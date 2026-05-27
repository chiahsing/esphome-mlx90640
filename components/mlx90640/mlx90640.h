#ifndef __MLX90640__
#define __MLX90640__

#include "esphome.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

#include "MLX90640_API.h"

namespace esphome::mlx90640 {

class MLX90640 : public PollingComponent, public i2c::I2CDevice, public AsyncWebHandler {
 public:
  MLX90640(web_server_base::WebServerBase *base);

  void set_min_temperature_sensor(sensor::Sensor *ts) { this->min_temperature_sensor_ = ts; }
  void set_max_temperature_sensor(sensor::Sensor *ts) { this->max_temperature_sensor_ = ts; };
  void set_mean_temperature_sensor(sensor::Sensor *ts) { this->mean_temperature_sensor_ = ts; };
  void set_min_temp(float min) { this->min_temp_ = min; }
  void set_max_temp(float max) { this->max_temp_ = max; }
  void set_invert(bool value) { this->invert_ = value; }

  void setup() override;
  void update() override;
  void dump_config() override;

  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *req) override;

  int i2c_read(uint8_t slaveAddr, uint16_t startAddress, uint16_t nMemAddressRead, uint16_t *data);
  int i2c_write(uint8_t slaveAddr, uint16_t writeAddress, uint16_t data);

 private:
  static constexpr int kNumPixels = MLX90640_LINE_SIZE * MLX90640_COLUMN_SIZE;
  static constexpr int kSpecMinValue = -40;
  static constexpr int kSpecMaxValue = 300;

  int render_image(uint8_t *buf, int size);

  web_server_base::WebServerBase *base_;
  sensor::Sensor *min_temperature_sensor_{nullptr};
  sensor::Sensor *max_temperature_sensor_{nullptr};
  sensor::Sensor *mean_temperature_sensor_{nullptr};
  float min_temp_;
  float max_temp_;
  bool invert_;

  int setup_status_ = -1;
  paramsMLX90640 params_mlx90640_;

  float pixels_[kNumPixels];

  // RGB values for each temp.
  float color_scale_;
  uint8_t rv_[256];
  uint8_t gv_[256];
  uint8_t bv_[256];
};

}  // namespace esphome::mlx90640

#endif
