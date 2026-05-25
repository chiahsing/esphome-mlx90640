# esphome-mlx90640
ESPHome component for MLX90640 thermal camera

This project is forked from https://github.com/Chill-Division/M5Stack-ESPHome
with modifications to make it to work under ESP-IDF.

Thermal image is available at http://your_device.local/thermal-camera.

Example usage:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/chiahsing/esphome-mlx90640
      ref: main
    components: [mlx90640]

esp32:
  board: esp32dev
  framework:
    type: esp-idf

i2c:
  sda: GPIO21
  scl: GPIO22
  frequency: 400kHz

mlx90640:
  address: 0x33
  update_interval: 5s
  min_temp: 15.0
  max_temp: 40.0
  min_temperature:
      name: "Min Temp"
  max_temperature:
      name: "Max Temp"
  mean_temperature:
      name: "Mean Temp"
```
