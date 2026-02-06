#include "epaper_t133a01.h"

#include <algorithm>

#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome::epaper_spi {
static constexpr const char *const TAG = "epaper_spi.t133a01";
static constexpr unsigned char GRAY_THRESHOLD = 50;

// T133A01 color palette indices (6-color ePaper)
// Maps to: Black(0), White(1), Green(2), Blue(3), Red(4), Yellow(5)
enum T133A01Color {
  T133A01_BLACK = 0x00,
  T133A01_WHITE = 0x01,
  T133A01_GREEN = 0x02,
  T133A01_BLUE = 0x03,
  T133A01_RED = 0x04,
  T133A01_YELLOW = 0x05,
};

// Register definitions
constexpr uint8_t REG_PSR = 0x00;    // Panel Setting Register
constexpr uint8_t REG_PWR = 0x01;    // Power Setting Register
constexpr uint8_t REG_POF = 0x02;    // Power Off Sequence
constexpr uint8_t REG_TRES = 0x61;   // Resolution Setting
constexpr uint8_t REG_CDI = 0x50;    // CDI Setting
constexpr uint8_t REG_PON = 0x04;    // Power On
constexpr uint8_t REG_DRF = 0x12;    // Display Refresh
constexpr uint8_t REG_SLEEP = 0x07;  // Sleep Register

uint8_t EPaperT133A01::color_to_palette_index(Color color) {
  // Check for grayscale (Black or White)
  unsigned char max_rgb = std::max({color.r, color.g, color.b});
  unsigned char min_rgb = std::min({color.r, color.g, color.b});

  if ((max_rgb - min_rgb) < GRAY_THRESHOLD) {
    // It's a shade of gray. Map to BLACK or WHITE based on luminance
    if ((static_cast<int>(color.r) + color.g + color.b) > 382) {
      return T133A01_WHITE;
    }
    return T133A01_BLACK;
  }

  // Check for primary/secondary colors
  bool r_on = (color.r > 128);
  bool g_on = (color.g > 128);
  bool b_on = (color.b > 128);

  // Yellow (R+G)
  if (r_on && g_on && !b_on) {
    return T133A01_YELLOW;
  }
  // Red (R)
  if (r_on && !g_on && !b_on) {
    return T133A01_RED;
  }
  // Green (G)
  if (!r_on && g_on && !b_on) {
    return T133A01_GREEN;
  }
  // Blue (B)
  if (!r_on && !g_on && b_on) {
    return T133A01_BLUE;
  }
  // Cyan (G+B) -> prefer Green
  if (!r_on && g_on && b_on) {
    return T133A01_GREEN;
  }
  // Magenta (R+B) -> prefer Red
  if (r_on && !g_on && b_on) {
    return T133A01_RED;
  }
  // All high -> White
  if (r_on && g_on && b_on) {
    return T133A01_WHITE;
  }
  // All low -> Black
  return T133A01_BLACK;
}

uint8_t EPaperT133A01::map_nibble_to_t133a01(uint8_t nibble) {
  // Color mapping from t133a01.md specification
  // Maps 4-bit color values to device palette indices
  switch (nibble) {
    case 0x0F:
      return 0x00;  // -> BLACK
    case 0x00:
      return 0x01;  // -> WHITE
    case 0x02:
      return 0x06;  // -> (specific palette)
    case 0x0B:
      return 0x02;  // -> GREEN
    case 0x0D:
      return 0x05;  // -> YELLOW
    case 0x06:
      return 0x03;  // -> BLUE
    default:
      return 0x01;  // -> WHITE (fallback)
  }
}

void EPaperT133A01::dump_config() {
  EPaperBase::dump_config();
  LOG_PIN("  CS1 Pin: ", this->cs1_pin_);
  LOG_PIN("  Enable Pin: ", this->enable_pin_);
}

void EPaperT133A01::setup_pins_() const {
  // Setup base pins (dc, reset, busy)
  EPaperBase::setup_pins_();

  // Setup CS1 pin
  if (this->cs1_pin_ != nullptr) {
    this->cs1_pin_->setup();              // OUTPUT
    this->cs1_pin_->digital_write(true);  // CS1 high (inactive)
  }

  // Setup enable pin
  if (this->enable_pin_ != nullptr) {
    this->enable_pin_->setup();              // OUTPUT
    this->enable_pin_->digital_write(true);  // Enable high (active)
  }
}

void EPaperT133A01::fill(Color color) {
  // If clipping is active, fall back to base implementation
  if (this->get_clipping().is_set()) {
    EPaperBase::fill(color);
    return;
  }

  uint8_t palette_index = color_to_palette_index(color);
  // Pack two 4-bit values per byte
  uint8_t pixel_byte = (palette_index << 4) | palette_index;

  this->buffer_.fill(pixel_byte);
  this->x_high_ = this->width_;
  this->y_high_ = this->height_;
  this->x_low_ = 0;
  this->y_low_ = 0;
}

void EPaperT133A01::clear() {
  this->fill(COLOR_ON);  // White
}

void EPaperT133A01::draw_pixel_at(int x, int y, Color color) {
  if (x < 0 || x >= this->get_width_internal() || y < 0 || y >= this->get_height_internal()) {
    return;
  }

  if (!this->rotate_coordinates_(x, y)) {
    return;
  }

  uint8_t palette_index = color_to_palette_index(color);
  size_t pos = (x + y * this->width_) / 2;
  uint8_t current = this->buffer_[pos];

  // Determine if we're in the upper or lower nibble
  if ((x + y * this->width_) % 2 == 0) {
    // Upper nibble
    this->buffer_[pos] = (palette_index << 4) | (current & 0x0F);
  } else {
    // Lower nibble
    this->buffer_[pos] = (current & 0xF0) | palette_index;
  }

  // Track dirty area
  this->x_low_ = std::min(this->x_low_, static_cast<uint16_t>(x));
  this->x_high_ = std::max(this->x_high_, static_cast<uint16_t>(x + 1));
  this->y_low_ = std::min(this->y_low_, static_cast<uint16_t>(y));
  this->y_high_ = std::max(this->y_high_, static_cast<uint16_t>(y + 1));
}

void EPaperT133A01::set_cs1_low_() {
  if (this->cs1_pin_) {
    this->cs1_pin_->digital_write(false);
  }
}

void EPaperT133A01::set_cs1_high_() {
  if (this->cs1_pin_) {
    this->cs1_pin_->digital_write(true);
  }
}

void EPaperT133A01::wait_busy_() {
  uint32_t timeout = millis() + 5000;  // 5 second timeout
  while (this->busy_pin_ && this->busy_pin_->digital_read()) {
    if (millis() > timeout) {
      ESP_LOGW(TAG, "Timeout waiting for display to be idle");
      return;
    }
    delay(10);
  }
}

bool EPaperT133A01::transfer_data() {
  // Transfer pixel data to display using two-pass method
  // Pass 1: First half of each row (bytes 0 to width/4)
  // Pass 2: Second half of each row (bytes width/4 to width/2)
  // Each byte contains two 4-bit pixels which are individually color-mapped

  // Send 0x10 command (Write Data)
  this->command(0x10);

  uint16_t bytes_per_row = this->width_ / 2;        // 800 bytes per row
  uint16_t bytes_per_block_row = this->width_ / 4;  // 400 bytes per pass
  uint32_t bytes_written = 0;

  ESP_LOGV(TAG, "Pass 1: Send first half of each row");

  // Pass 1: Send first half of each row with color mapping
  for (uint16_t y = 0; y < this->height_; y++) {
    if (y % 10 == 0) {
      App.feed_wdt();
    }
    for (uint16_t col = 0; col < bytes_per_block_row; col++) {
      size_t pos = y * bytes_per_row + col;
      uint8_t pixel_byte = this->buffer_[pos];

      // Extract and map both nibbles
      uint8_t upper_nibble = (pixel_byte >> 4) & 0x0F;
      uint8_t lower_nibble = pixel_byte & 0x0F;
      uint8_t mapped_upper = map_nibble_to_t133a01(upper_nibble);
      uint8_t mapped_lower = map_nibble_to_t133a01(lower_nibble);

      // Combine mapped values and send as single byte
      this->write_byte((mapped_upper << 4) | mapped_lower);
      bytes_written++;
    }
  }

  ESP_LOGV(TAG, "Pass 2: Send second half of each row");

  // Pass 2: Send second half of each row with color mapping
  for (uint16_t y = 0; y < this->height_; y++) {
    if (y % 10 == 0) {
      App.feed_wdt();
    }
    for (uint16_t col = 0; col < bytes_per_block_row; col++) {
      size_t pos = y * bytes_per_row + bytes_per_block_row + col;
      uint8_t pixel_byte = this->buffer_[pos];

      // Extract and map both nibbles
      uint8_t upper_nibble = (pixel_byte >> 4) & 0x0F;
      uint8_t lower_nibble = pixel_byte & 0x0F;
      uint8_t mapped_upper = map_nibble_to_t133a01(upper_nibble);
      uint8_t mapped_lower = map_nibble_to_t133a01(lower_nibble);

      // Combine mapped values and send as single byte
      this->write_byte((mapped_upper << 4) | mapped_lower);
      bytes_written++;
    }
  }

  ESP_LOGV(TAG, "Transfer complete: %u bytes sent", bytes_written);
  return true;
}

void EPaperT133A01::refresh_screen(bool partial) {
  ESP_LOGV(TAG, "Refresh screen");
  this->set_cs1_low_();
  this->command(REG_DRF);  // Display Refresh
  this->set_cs1_high_();
  this->wait_busy_();
}

void EPaperT133A01::power_on() {
  ESP_LOGV(TAG, "Power on");
  this->set_cs1_low_();
  this->command(REG_PON);  // Power On
  this->set_cs1_high_();
  this->wait_busy_();
}

void EPaperT133A01::power_off() {
  ESP_LOGV(TAG, "Power off");
  this->set_cs1_low_();
  this->command(REG_POF);  // Power Off Sequence
  this->set_cs1_high_();
  this->wait_busy_();
}

void EPaperT133A01::deep_sleep() {
  ESP_LOGV(TAG, "Deep sleep");
  this->set_cs1_low_();
  this->cmd_data(REG_SLEEP, {0xA5});
  this->set_cs1_high_();
}

}  // namespace esphome::epaper_spi
