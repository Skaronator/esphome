#pragma once

#include "epaper_spi.h"
#include "esphome/core/gpio.h"

namespace esphome::epaper_spi {

/**
 * T133A01 13.3" Color E-Paper Display Driver
 *
 * This display requires TWO chip select pins:
 * - cs_pin: Used for data transfer commands (0x10 DTM)
 * - cs2_pin: Used for configuration commands (init, power, refresh, etc.)
 *
 * This is a workaround implementation using manual GPIO control for cs2_pin
 * since ESPHome's SPI infrastructure only supports a single CS pin.
 */
class EPaperT133A01 : public EPaperBase {
 public:
  EPaperT133A01(const char *name, uint16_t width, uint16_t height, const uint8_t *init_sequence,
                size_t init_sequence_length)
      : EPaperBase(name, width, height, init_sequence, init_sequence_length, DISPLAY_TYPE_COLOR) {
    this->buffer_length_ = width * height / 2;  // 2 pixels per byte
  }

  void set_cs2_pin(GPIOPin *cs2_pin) { this->cs2_pin_ = cs2_pin; }

  void setup() override;
  void fill(Color color) override;
  void clear() override;

 protected:
  void refresh_screen(bool partial) override;
  void power_on() override;
  void power_off() override;
  void deep_sleep() override;
  void draw_pixel_at(int x, int y, Color color) override;
  bool transfer_data() override;

  // Override command methods to use cs2_pin
  void command_cs2(uint8_t value);
  void cmd_data_cs2(uint8_t command, const uint8_t *ptr, size_t length);
  void cmd_data_cs2(uint8_t command, std::initializer_list<uint8_t> data) {
    this->cmd_data_cs2(command, data.begin(), data.size());
  }

  bool initialise(bool partial) override;

  GPIOPin *cs2_pin_{nullptr};
};

}  // namespace esphome::epaper_spi
