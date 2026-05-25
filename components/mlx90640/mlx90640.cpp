#include "mlx90640.h"

namespace esphome::mlx90640 {
namespace {

static const char *TAG = "MLX90640";

MLX90640 *g_mlx90640 = nullptr;

template<typename T> T clamp(const T &value, const T &min_value, const T &max_value) {
  if (value < min_value) {
    return min_value;
  } else if (value > max_value) {
    return max_value;
  } else {
    return value;
  }
}

inline void write_uint16_le(uint8_t *&ptr, uint16_t value) {
  *ptr++ = value & 0xff;
  *ptr++ = (value >> 8) & 0xff;
}

inline void write_uint32_le(uint8_t *&ptr, uint32_t value) {
  write_uint16_le(ptr, value);
  write_uint16_le(ptr, value >> 16);
}

}  // namespace

MLX90640::MLX90640(web_server_base::WebServerBase *base) : base_(base) { g_mlx90640 = this; }

void MLX90640::setup() {
  // Color table initialization
  color_scale_ = 255.0 / (max_temp_ - min_temp_);

  // #0000FF to #00FFFF
  int r = 0, g = 0, b = 255;
  for (int i = 0; i < 64; ++i) {
    rv_[i] = r, gv_[i] = g, bv_[i] = b;
    g += 4;
  }

  // #00FFFF to #00FF00
  r = 0, g = 255, b = 255;
  for (int i = 64; i < 128; ++i) {
    rv_[i] = r, gv_[i] = g, bv_[i] = b;
    b -= 4;
  }

  // #00FF00 to #FFFF00
  r = 0, g = 255, b = 0;
  for (int i = 128; i < 192; ++i) {
    rv_[i] = r, gv_[i] = g, bv_[i] = b;
    r += 4;
  }

  // #FFFF00 to #FF0000
  r = 255, g = 255, b = 0;
  for (int i = 192; i < 256; ++i) {
    rv_[i] = r, gv_[i] = g, bv_[i] = b;
    g -= 4;
  }

  // MLX90640 initialization
  uint16_t ee[MLX90640_EEPROM_DUMP_NUM];
  int status = MLX90640_DumpEE(address_, ee);
  if (status != 0) {
    ESP_LOGE(TAG, "Failed to load system parameters");
    setup_status_ = 1;
    return;
  }

  status = MLX90640_ExtractParameters(ee, &params_mlx90640_);
  if (status != 0) {
    ESP_LOGE(TAG, "Failed to extract parameters");
    setup_status_ = 2;
    return;
  }

  status = MLX90640_SetResolution(address_, 0x02);  // 18-bit
  if (status != 0) {
    ESP_LOGE(TAG, "Failed to set resolution");
    setup_status_ = 3;
    return;
  }

  status = MLX90640_SetRefreshRate(address_, 0x04);  // 8Hz
  if (status != 0) {
    ESP_LOGE(TAG, "Failed to set refresh rate");
    setup_status_ = 4;
    return;
  }

  status = MLX90640_SetChessMode(address_);  // 8Hz
  if (status != 0) {
    ESP_LOGE(TAG, "Failed to set chess mode");
    setup_status_ = 5;
    return;
  }

  // Register as a handler for the web server
  base_->add_handler(this);

  setup_status_ = 0;
}

void MLX90640::update() {
  if (setup_status_ != 0) {
    return;
  }

  int status = 0;

  uint16_t frame[834];
  // Read two subpages.
  for (int i = 0; i < 2; ++i) {
    status = MLX90640_GetFrameData(address_, frame);
    if (status < 0) {
      ESP_LOGE(TAG, "Failed to get frame data: %d", status);
      return;
    }
  }

  float vdd = MLX90640_GetVdd(frame, &params_mlx90640_);
  float ta = MLX90640_GetTa(frame, &params_mlx90640_);  // ambeient temp
  float tr = ta - 8;                                    // reflected temp
  float emissivity = 0.95;                              // industry best practice
  MLX90640_CalculateTo(frame, &params_mlx90640_, emissivity, tr, pixels_);

  int mode = MLX90640_GetCurMode(address_);
  MLX90640_BadPixelsCorrection(params_mlx90640_.brokenPixels, pixels_, mode, &params_mlx90640_);

  float max_value = min_temp_;
  float min_value = max_temp_;
  float total = 0;
  for (int i = 0; i < kNumPixels; ++i) {
    if (pixels_[i] > max_value) {
      max_value = pixels_[i];
    }
    if (pixels_[i] < min_value) {
      min_value = pixels_[i];
    }
    total += pixels_[i];
  }
  float mean_value = total / kNumPixels;

  if (max_value > kSpecMaxValue || min_value < kSpecMinValue) {
    ESP_LOGE(TAG, "Failed to read pixel values");
    return;
  }

  this->min_temperature_sensor_->publish_state(min_value);
  this->max_temperature_sensor_->publish_state(max_value);
  this->mean_temperature_sensor_->publish_state(mean_value);
}

void MLX90640::dump_config() {
  ESP_LOGCONFIG(TAG, "MLX90640:");
  ESP_LOGCONFIG(TAG, "  Setup status: %d", setup_status_);
  LOG_UPDATE_INTERVAL(this);
}

bool MLX90640::canHandle(AsyncWebServerRequest *request) const {
  char url[AsyncWebServerRequest::URL_BUF_SIZE];
  return request->url_to(url) == ESPHOME_F("/thermal-camera") && request->method() == HTTP_GET;
}

void MLX90640::handleRequest(AsyncWebServerRequest *req) {
  static uint8_t buffer[3072];
  auto len = render_image(buffer, sizeof(buffer));

  AsyncWebServerResponse *response = req->beginResponse(200, ESPHOME_F("image/bmp"), buffer, len);
  response->addHeader(ESPHOME_F("Content-Disposition"), ESPHOME_F("inline; filename=thermal.bmp"));

  req->send(response);
}

int MLX90640::i2c_read(uint8_t slaveAddr, uint16_t startAddress, uint16_t nMemAddressRead, uint16_t *data) {
  int status = read_register16(startAddress, reinterpret_cast<uint8_t *>(data), nMemAddressRead * 2);
  if (status != i2c::NO_ERROR) {
    ESP_LOGE(TAG, "Failed to read %d words from %d from I2C: %d", nMemAddressRead, startAddress, status);
    if (status == i2c::ERROR_NOT_ACKNOWLEDGED) {
      return -1;
    } else {
      return -(1000 + status);
    }
  }

  for (int i = 0; i < nMemAddressRead; ++i) {
    data[i] = i2c::i2ctohs(data[i]);
  }

  return 0;
}

int MLX90640::i2c_write(uint8_t slaveAddr, uint16_t writeAddress, uint16_t data) {
  uint16_t tmp = i2c::htoi2cs(data);
  int status = write_register16(writeAddress, reinterpret_cast<uint8_t *>(&tmp), sizeof(tmp));
  if (status != i2c::NO_ERROR) {
    ESP_LOGE(TAG, "Failed to write to %d to I2C: %d", writeAddress, status);
    if (status == i2c::ERROR_NOT_ACKNOWLEDGED) {
      return -1;
    } else {
      return -(1000 + status);
    }
  }

  return 0;
}

int MLX90640::render_image(uint8_t *buf, int size) {
  uint8_t *ptr = buf;
  constexpr int kWidth = MLX90640_LINE_SIZE;
  constexpr int kHeight = MLX90640_COLUMN_SIZE;

  // # of padding byte per line
  int padding = 4 - ((kWidth * 3) % 4);
  if (padding == 4) {
    padding = 0;
  }
  int padded_size = (kWidth * 3 + padding) * kHeight;

  constexpr int kFileHeaderSize = 14;
  constexpr int kDibHeaderSize = 40;
  constexpr int kHeaderSize = kFileHeaderSize + kDibHeaderSize;

  // 14 bytes file header
  *ptr++ = 'B';
  *ptr++ = 'M';
  write_uint32_le(ptr, padded_size + kHeaderSize);  // file size
  write_uint32_le(ptr, 0);                          // reserved
  write_uint32_le(ptr, kHeaderSize);                // offset

  // 40 bytes DIB header (BITMAPINFOHEADER)
  write_uint32_le(ptr, kDibHeaderSize);  // offset
  write_uint32_le(ptr, kWidth);          // width
  write_uint32_le(ptr, kHeight);         // height
  write_uint16_le(ptr, 1);               // # of color plane
  write_uint16_le(ptr, 24);              // # of bits per pixel
  write_uint32_le(ptr, 0);               // compression
  write_uint32_le(ptr, padded_size);     // image size
  write_uint32_le(ptr, 0);               // X resolution (px/m)
  write_uint32_le(ptr, 0);               // Y resolution (px/m)
  write_uint32_le(ptr, 0);               // # of colors
  write_uint32_le(ptr, 0);               // # of important colors

  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      float color_f = (pixels_[x + (kWidth * y)] - min_temp_) * color_scale_;
      int color_i = static_cast<int>(clamp(color_f, 0.0f, 255.0f));

      // Write in BGR instead of RGB
      *ptr++ = bv_[color_i];
      *ptr++ = gv_[color_i];
      *ptr++ = rv_[color_i];
    }
    for (int i = 0; i < padding; ++i) {
      *ptr++ = 0;
    }
  }

  return ptr - buf;
}

}  // namespace esphome::mlx90640

extern "C" {

int MLX90640_I2CRead(uint8_t slaveAddr, uint16_t startAddress, uint16_t nMemAddressRead, uint16_t *data) {
  return esphome::mlx90640::g_mlx90640->i2c_read(slaveAddr, startAddress, nMemAddressRead, data);
}

int MLX90640_I2CWrite(uint8_t slaveAddr, uint16_t writeAddress, uint16_t data) {
  return esphome::mlx90640::g_mlx90640->i2c_write(slaveAddr, writeAddress, data);
}

}  // extern "C"
