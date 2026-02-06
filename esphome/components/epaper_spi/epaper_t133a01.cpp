#include "epaper_t133a01.h"

#include <algorithm>

#include "esphome/core/log.h"

namespace esphome::epaper_spi {
static constexpr const char *const TAG = "epaper_spi.t133a01";

// Color conversion from official T133A01 driver
// Threshold for determining grayscale
static constexpr uint8_t GRAY_THRESHOLD = 50;

// Color definitions matching official driver
enum T133A01Color {
  BLACK = 0,
  WHITE = 1,
  GREEN = 2,
  BLUE = 3,
  RED = 4,
  YELLOW = 5,
  ORANGE = 6,
  CYAN = 7,
};

/**
 * Color transformation logic from official T133A01 driver COLOR_GET macro
 *
 * The macro expands to:
 *   ((((R) >> 5) << 5) | (((G) >> 5) << 2) | ((B) >> 6))
 *
 * This maps 24-bit RGB to a 3-3-2 format (8 levels R, 8 levels G, 4 levels B)
 * Then that value is used to lookup colors in a 256-entry table.
 *
 * For ESPHome, we'll do direct color matching instead of the full lookup table.
 */
static uint8_t color_to_t133a01(Color color) {
  // Check for grayscale first
  uint8_t max_rgb = std::max({color.r, color.g, color.b});
  uint8_t min_rgb = std::min({color.r, color.g, color.b});

  if ((max_rgb - min_rgb) < GRAY_THRESHOLD) {
    // Grayscale - map to black or white
    if ((static_cast<int>(color.r) + color.g + color.b) > 382) {
      return WHITE;
    }
    return BLACK;
  }

  // Check for primary/secondary colors
  bool r_high = (color.r > 128);
  bool g_high = (color.g > 128);
  bool b_high = (color.b > 128);

  // Primary colors
  if (r_high && !g_high && !b_high)
    return RED;
  if (!r_high && g_high && !b_high)
    return GREEN;
  if (!r_high && !g_high && b_high)
    return BLUE;

  // Secondary colors
  if (r_high && g_high && !b_high)
    return YELLOW;
  if (r_high && !g_high && b_high)
    return RED;  // Magenta -> Red (closest match)
  if (!r_high && g_high && b_high)
    return CYAN;

  // Orange (special case - red-biased yellow)
  if (r_high && color.g > 64 && color.g < 192 && !b_high)
    return ORANGE;

  // Fallback to white or black
  if (r_high || g_high || b_high)
    return WHITE;
  return BLACK;
}

void EPaperT133A01::setup() {
  if (this->cs2_pin_ != nullptr) {
    this->cs2_pin_->setup();
    this->cs2_pin_->digital_write(true);  // CS2 inactive (high)
  }
  EPaperBase::setup();
}

void EPaperT133A01::command_cs2(uint8_t value) {
  if (this->cs2_pin_ == nullptr) {
    ESP_LOGE(TAG, "CS2 pin not configured!");
    return;
  }

  this->enable();
  this->dc_pin_->digital_write(false);   // Command mode
  this->cs2_pin_->digital_write(false);  // CS2 active (low)
  this->write_byte(value);
  this->cs2_pin_->digital_write(true);  // CS2 inactive (high)
  this->disable();
}

void EPaperT133A01::cmd_data_cs2(uint8_t command, const uint8_t *ptr, size_t length) {
  if (this->cs2_pin_ == nullptr) {
    ESP_LOGE(TAG, "CS2 pin not configured!");
    return;
  }

  this->enable();
  // Send command
  this->dc_pin_->digital_write(false);   // Command mode
  this->cs2_pin_->digital_write(false);  // CS2 active (low)
  this->write_byte(command);
  this->cs2_pin_->digital_write(true);  // CS2 inactive (high)

  if (length > 0) {
    // Send data
    this->dc_pin_->digital_write(true);    // Data mode
    this->cs2_pin_->digital_write(false);  // CS2 active (low)
    this->write_array(ptr, length);
    this->cs2_pin_->digital_write(true);  // CS2 inactive (high)
  }
  this->disable();
}

bool EPaperT133A01::initialise(bool partial) {
  // Use base class init, but override to use cs2_pin for configuration
  if (this->init_sequence_ != nullptr && this->init_sequence_length_ > 0) {
    const uint8_t *sequence = this->init_sequence_;
    size_t remaining = this->init_sequence_length_;

    while (remaining > 0) {
      uint8_t cmd = *sequence++;
      remaining--;

      if (cmd == DELAY_FLAG) {
        if (remaining < 1) {
          ESP_LOGE(TAG, "Invalid init sequence: delay flag without value");
          return true;
        }
        uint8_t delay_ms = *sequence++;
        remaining--;
        delay(delay_ms);
        continue;
      }

      if (remaining < 1) {
        ESP_LOGE(TAG, "Invalid init sequence: command without length");
        return true;
      }

      uint8_t data_len = *sequence++;
      remaining--;

      if (remaining < data_len) {
        ESP_LOGE(TAG, "Invalid init sequence: not enough data for command 0x%02X", cmd);
        return true;
      }

      // Use cs2_pin for configuration commands
      this->cmd_data_cs2(cmd, sequence, data_len);

      sequence += data_len;
      remaining -= data_len;
    }
  }

  return true;  // Init complete
}

void EPaperT133A01::power_on() {
  ESP_LOGV(TAG, "Power on");
  this->command_cs2(0x04);  // PON - Power On
}

void EPaperT133A01::power_off() {
  ESP_LOGV(TAG, "Power off");
  this->cmd_data_cs2(0x02, {0x00});  // POF - Power Off
}

void EPaperT133A01::refresh_screen(bool partial) {
  ESP_LOGV(TAG, "Refresh screen");
  this->cmd_data_cs2(0x12, {0x00});  // DRF - Display Refresh
}

void EPaperT133A01::deep_sleep() {
  ESP_LOGV(TAG, "Deep sleep");
  this->cmd_data_cs2(0x07, {0xA5});  // DSLP - Deep Sleep
}

void EPaperT133A01::fill(Color color) {
  if (this->get_clipping().is_set()) {
    EPaperBase::fill(color);
    return;
  }

  uint8_t pixel_color = color_to_t133a01(color);
  // Store 2 pixels per byte (4 bits each)
  this->buffer_.fill(pixel_color | (pixel_color << 4));
}

void EPaperT133A01::clear() {
  // Clear to white (like real paper)
  this->fill(COLOR_ON);
}

void HOT EPaperT133A01::draw_pixel_at(int x, int y, Color color) {
  if (!this->rotate_coordinates_(x, y))
    return;

  uint8_t pixel_bits = color_to_t133a01(color);
  uint32_t pixel_position = x + y * this->get_width_internal();
  uint32_t byte_position = pixel_position / 2;
  uint8_t original = this->buffer_[byte_position];

  if ((pixel_position & 1) != 0) {
    // Odd pixel (low nibble)
    this->buffer_[byte_position] = (original & 0xF0) | pixel_bits;
  } else {
    // Even pixel (high nibble)
    this->buffer_[byte_position] = (original & 0x0F) | (pixel_bits << 4);
  }
}

bool HOT EPaperT133A01::transfer_data() {
  const uint32_t start_time = App.get_loop_component_start_time();
  const size_t buffer_length = this->buffer_length_;

  if (this->current_data_index_ == 0) {
    // Start data transfer command - use regular CS pin (not cs2_pin)
    this->command(0x10);  // DTM - Data Transfer Mode
  }

  size_t buf_idx = 0;
  uint8_t bytes_to_send[MAX_TRANSFER_SIZE];

  while (this->current_data_index_ != buffer_length) {
    bytes_to_send[buf_idx++] = this->buffer_[this->current_data_index_++];

    if (buf_idx == sizeof bytes_to_send) {
      this->start_data_();
      this->write_array(bytes_to_send, buf_idx);
      this->disable();
      ESP_LOGV(TAG, "Wrote %d bytes at %ums", buf_idx, (unsigned) millis());
      buf_idx = 0;

      if (millis() - start_time > MAX_TRANSFER_TIME) {
        // Let the main loop run and come back next loop
        return false;
      }
    }
  }

  // Finish any remaining data
  if (buf_idx != 0) {
    this->start_data_();
    this->write_array(bytes_to_send, buf_idx);
    this->disable();
  }

  this->current_data_index_ = 0;
  return true;  // Transfer complete
}

}  // namespace esphome::epaper_spi
