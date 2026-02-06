#pragma once

#include "epaper_spi.h"

namespace esphome::epaper_spi {

class EPaperT133A01 : public EPaperBase {
 public:
  EPaperT133A01(const char *name, uint16_t width, uint16_t height, const uint8_t *init_sequence,
                size_t init_sequence_length)
      : EPaperBase(name, width, height, init_sequence, init_sequence_length, DISPLAY_TYPE_COLOR) {
    this->buffer_length_ = width * height / 2;  // 4 bpp (6-color), 2 pixels per byte
  }

  void set_cs1_pin(GPIOPin *cs1_pin) { this->cs1_pin_ = cs1_pin; }
  void set_enable_pin(GPIOPin *enable_pin) { this->enable_pin_ = enable_pin; }

  void dump_config() override;
  void fill(Color color) override;
  void clear() override;

 protected:
  void setup_pins_() const;
  void refresh_screen(bool partial) override;
  void power_on() override;
  void power_off() override;
  void deep_sleep() override;
  void draw_pixel_at(int x, int y, Color color) override;

  bool transfer_data() override;

 private:
  static constexpr const char *TAG = "epaper_spi.t133a01";

  // Color palette mapping for T133A01
  static uint8_t color_to_palette_index(Color color);
  static uint8_t map_nibble_to_t133a01(uint8_t nibble);

  GPIOPin *cs1_pin_{nullptr};
  GPIOPin *enable_pin_{nullptr};

  // Helper methods for CS1 and busy handling
  void set_cs1_low_();
  void set_cs1_high_();
  void wait_busy_();
};

}  // namespace esphome::epaper_spi
