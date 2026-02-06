#include "epaper_spi_t133a01.h"

#include <algorithm>

#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome::epaper_spi {
static constexpr const char *const TAG = "epaper_spi.t133a01";

// Register definitions from Arduino driver (complete_source.cpp)
constexpr uint8_t REG_PSR = 0x00;    // Panel Setting Register
constexpr uint8_t REG_PWR = 0x01;    // Power Setting Register
constexpr uint8_t REG_POF = 0x02;    // Power Off Sequence
constexpr uint8_t REG_PON = 0x04;    // Power On
constexpr uint8_t REG_BTST = 0x06;   // Booster Soft Start
constexpr uint8_t REG_DM = 0x10;     // Data Mode (Write to frame buffer)
constexpr uint8_t REG_DRF = 0x12;    // Display Refresh
constexpr uint8_t REG_PLL = 0x30;    // PLL Control
constexpr uint8_t REG_TSE = 0x41;    // Temperature Sensor Enable
constexpr uint8_t REG_TRES = 0x61;   // Resolution Setting
constexpr uint8_t REG_AMV = 0x37;    // Address Mode
constexpr uint8_t REG_CDI = 0x50;    // CDI (Color Component Driving Interface)
constexpr uint8_t REG_r60 = 0x60;    // TCON Driving Mode
constexpr uint8_t REG_r74 = 0x74;    // Waveform/LUT
constexpr uint8_t REG_r86 = 0x86;    // Timing Control
constexpr uint8_t REG_rb0 = 0xB0;    // Power On Sequence
constexpr uint8_t REG_rb1 = 0xB1;    // Panel Breaking
constexpr uint8_t REG_rb6 = 0xB6;    // Border Waveform
constexpr uint8_t REG_rb7 = 0xB7;    // Border Control
constexpr uint8_t REG_rf0 = 0xF0;    // Advanced Timing Configuration
constexpr uint8_t REG_CCSET = 0xE0;  // Color Component Set
constexpr uint8_t REG_SLEEP = 0x07;  // Sleep Register

// Register values from Arduino driver (complete_source.cpp lines 136059-136139)
static constexpr uint8_t PSR_V[2] = {0xDF, 0x69};
static constexpr uint8_t PWR_V[6] = {0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38};
static constexpr uint8_t POF_V[1] = {0x00};
static constexpr uint8_t BTST_P_V[2] = {0xD8, 0x18};
static constexpr uint8_t BTST_N_V[2] = {0xD8, 0x18};
static constexpr uint8_t TRES_V[4] = {0x04, 0xB0, 0x03, 0x20};  // 1200x1600
static constexpr uint8_t AMV_V[2] = {0x01, 0x00};
static constexpr uint8_t CDI_V[1] = {0x37};
static constexpr uint8_t r60_V[2] = {0x03, 0x03};
static constexpr uint8_t r74_V[9] = {0xC0, 0x1C, 0x1C, 0xCC, 0xCC, 0xCC, 0x15, 0x15, 0x55};
static constexpr uint8_t r86_V[1] = {0x10};
static constexpr uint8_t rb0_V[1] = {0x01};
static constexpr uint8_t rb1_V[1] = {0x02};
static constexpr uint8_t rb6_V[1] = {0x07};
static constexpr uint8_t rb7_V[1] = {0x01};
static constexpr uint8_t rf0_V[6] = {0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};
static constexpr uint8_t SLEEP_V[1] = {0xa5};
static constexpr uint8_t DRF_V[1] = {0x01};
static constexpr uint8_t CCSET_V[1] = {0x01};  // Color mode: 0x01 for current colors

// COLOR_GET macro from manufacturer - maps internal color to display color format
// This is applied during data transfer, not during draw operations
static uint8_t color_get(uint8_t color) {
  switch (color) {
    case 0x0F:
      return 0x00;  // White (0x0F) → 0x00
    case 0x00:
      return 0x01;  // Black (0x00) → 0x01
    case 0x02:
      return 0x06;  // Green (0x02) → 0x06
    case 0x0B:
      return 0x02;  // ? (0x0B) → 0x02
    case 0x0D:
      return 0x05;  // ? (0x0D) → 0x05
    case 0x06:
      return 0x03;  // ? (0x06) → 0x03
    default:
      return 0x01;  // Default to white
  }
}

uint8_t EPaperT133A01::color_to_palette_index(Color color) {
  // Convert RGB color to manufacturer's 6-color palette format
  // From TFT_eSPI.h USE_COLORFULL_EPAPER section:
  //   TFT_BLACK  = 0x0F
  //   TFT_WHITE  = 0x00
  //   TFT_BLUE   = 0x0D
  //   TFT_YELLOW = 0x0B
  //   TFT_GREEN  = 0x02
  //   TFT_RED    = 0x06

  // Check for grayscale by looking at saturation
  uint8_t max_rgb = std::max({color.r, color.g, color.b});
  uint8_t min_rgb = std::min({color.r, color.g, color.b});
  uint8_t saturation = max_rgb - min_rgb;

  if (saturation < 50) {
    // Grayscale - map by luminance
    uint16_t luminance = static_cast<uint16_t>(color.r) + color.g + color.b;
    if (luminance > 382) {
      return 0x00;  // White (manufacturer format)
    }
    return 0x0F;  // Black (manufacturer format)
  }

  // Colored pixel - find dominant color
  bool r_dom = (color.r > color.g) && (color.r > color.b);
  bool g_dom = (color.g > color.r) && (color.g > color.b);
  bool b_dom = (color.b > color.r) && (color.b > color.g);

  // Red dominates
  if (r_dom) {
    // Check if also has significant green -> Yellow
    if (color.g > 100) {
      return 0x0B;  // Yellow (manufacturer format)
    }
    return 0x06;  // Red (manufacturer format)
  }

  // Green dominates
  if (g_dom) {
    return 0x02;  // Green (manufacturer format)
  }

  // Blue dominates
  if (b_dom) {
    return 0x0D;  // Blue (manufacturer format)
  }

  // Fallback
  return 0x00;  // Default to White (manufacturer format)
}

uint8_t EPaperT133A01::map_nibble_to_t133a01(uint8_t nibble) {
  // This function maps from the internal 4-bit representation to device palette
  // The nibble value represents a pre-computed palette index (0-5)
  // We pass it through directly since the buffer already stores palette indices
  return nibble & 0x0F;
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

  // Setup enable pin - MUST be HIGH for display to work!
  if (this->enable_pin_ != nullptr) {
    this->enable_pin_->setup();              // OUTPUT
    this->enable_pin_->digital_write(true);  // Enable high (active)
    ESP_LOGD(TAG, "Enable pin set HIGH");
  } else {
    ESP_LOGW(TAG, "No enable pin configured! Display may not work.");
  }

  // Debug: Log all pin states after setup
  ESP_LOGD(TAG, "Pin states after setup:");
  if (this->cs1_pin_) {
    ESP_LOGD(TAG, "  CS1: HIGH (inactive)");
  }
  if (this->busy_pin_) {
    bool busy_state = this->busy_pin_->digital_read();
    ESP_LOGD(TAG, "  BUSY: digital_read=%d (physical %s)", busy_state, busy_state ? "LOW/busy" : "HIGH/idle");
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
  if (!this->busy_pin_) {
    return;  // No busy pin configured
  }

  uint32_t start = millis();
  uint32_t timeout = millis() + 60000;  // 60 second timeout (e-paper refresh can take 5-30s)

  // Initial delay before checking busy pin (as per manufacturer's CHECK_BUSY macro)
  delay(10);

  // Debug: log initial pin state
  bool initial_state = this->busy_pin_->digital_read();
  ESP_LOGD(TAG, "Busy pin initial state: %s (digital_read=%d)", initial_state ? "BUSY" : "IDLE", initial_state);

  // Manufacturer's CHECK_BUSY waits until digitalRead(TFT_BUSY) is HIGH (idle)
  // With inverted: true in YAML, digital_read() returns TRUE when physical pin is LOW
  // Physical pin: LOW = busy, HIGH = idle
  // So with inversion: digital_read() TRUE = physical LOW = busy
  //                    digital_read() FALSE = physical HIGH = idle
  // We wait while digital_read() is TRUE (busy)
  while (this->busy_pin_->digital_read()) {
    App.feed_wdt();  // Feed watchdog during long wait

    if (millis() > timeout) {
      ESP_LOGW(TAG, "Timeout waiting for display to be idle after %u ms", millis() - start);
      return;
    }

    delay(100);  // Check every 100ms during refresh
  }

  ESP_LOGV(TAG, "Display became idle after %u ms", millis() - start);
}

bool EPaperT133A01::transfer_data() {
  // Transfer pixel data to display using two-pass method from Arduino driver
  // The T133A01 requires data to be sent in two passes with specific CS1 toggling:
  // Pass 1: CS1 HIGH, main CS LOW - first half of each row
  // Pass 2: CS1 LOW (main CS still handles SPI) - second half of each row
  // Each byte contains two 4-bit pixels with palette indices

  ESP_LOGD(TAG, "Starting data transfer: %u x %u pixels", this->width_, this->height_);

  // Send color mode configuration (CCSET) as per manufacturer's EPD_PUSH_NEW_COLORS
  // Sequence: CS1 LOW → send CCSET → CS1 HIGH → wait busy → delay
  ESP_LOGV(TAG, "CCSET: CS1 LOW, sending 0xE0 0x01");
  this->set_cs1_low_();
  this->dc_pin_->digital_write(false);
  this->enable();
  this->write_byte(REG_CCSET);
  this->dc_pin_->digital_write(true);
  this->write_array(CCSET_V, sizeof(CCSET_V));
  this->disable();
  this->set_cs1_high_();  // CS1 HIGH before busy wait (per manufacturer)
  ESP_LOGV(TAG, "CCSET: CS1 HIGH, waiting for busy");
  this->wait_busy_();  // Wait with CS1 HIGH
  delay(10);

  uint16_t bytes_per_row = this->width_ / 2;        // 600 bytes per row (1200 pixels / 2)
  uint16_t bytes_per_block_row = this->width_ / 4;  // 300 bytes per pass
  uint32_t bytes_written = 0;

  // Pass 1: CS1 HIGH, main CS handles SPI - first half of each row
  // Manufacturer: digitalWrite(TFT_CS1, HIGH); digitalWrite(TFT_CS, LOW); spi.transfer(R10_DTM);
  ESP_LOGV(TAG, "Data Pass 1: CS1 HIGH, first half of each row");
  this->set_cs1_high_();  // CS1 HIGH for pass 1
  this->dc_pin_->digital_write(false);
  this->enable();            // Main CS LOW
  this->write_byte(REG_DM);  // DTM command (0x10)
  this->dc_pin_->digital_write(true);

  for (uint16_t y = 0; y < this->height_; y++) {
    if (y % 100 == 0) {
      App.feed_wdt();
      ESP_LOGV(TAG, "Pass 1 progress: row %u/%u", y, this->height_);
    }
    for (uint16_t col = 0; col < bytes_per_block_row; col++) {
      size_t pos = y * bytes_per_row + col;
      if (pos < this->buffer_.size()) {
        // Apply COLOR_GET transformation as per manufacturer's code
        uint8_t b = this->buffer_[pos];
        uint8_t temp1 = (b >> 4) & 0x0F;
        uint8_t temp2 = b & 0x0F;
        uint8_t transformed = (color_get(temp1) << 4) | color_get(temp2);
        this->write_byte(transformed);
        bytes_written++;
      }
    }
  }
  this->disable();  // Main CS HIGH

  // Pass 2: CS1 LOW, main CS stays HIGH - only CS1 controls this transfer
  // Manufacturer: digitalWrite(TFT_CS, HIGH); digitalWrite(TFT_CS1, LOW);
  // They do NOT set main CS LOW for pass 2 - only CS1 controls the second half
  ESP_LOGV(TAG, "Data Pass 2: CS1 LOW (CS stays HIGH), second half of each row");
  this->disable();       // Ensure main CS is HIGH (in case it was low)
  this->set_cs1_low_();  // CS1 LOW for pass 2
  this->dc_pin_->digital_write(false);
  // Note: NOT calling enable() - main CS stays HIGH, only CS1 is LOW
  this->write_byte(REG_DM);  // DTM command (0x10) - goes to CS1 selected chip
  this->dc_pin_->digital_write(true);

  for (uint16_t y = 0; y < this->height_; y++) {
    if (y % 100 == 0) {
      App.feed_wdt();
      ESP_LOGV(TAG, "Pass 2 progress: row %u/%u", y, this->height_);
    }
    for (uint16_t col = 0; col < bytes_per_block_row; col++) {
      size_t pos = y * bytes_per_row + bytes_per_block_row + col;
      if (pos < this->buffer_.size()) {
        // Apply COLOR_GET transformation as per manufacturer's code
        uint8_t b = this->buffer_[pos];
        uint8_t temp1 = (b >> 4) & 0x0F;
        uint8_t temp2 = b & 0x0F;
        uint8_t transformed = (color_get(temp1) << 4) | color_get(temp2);
        this->write_byte(transformed);
        bytes_written++;
      }
    }
  }
  // Note: No disable() here since we didn't call enable() for pass 2
  // Main CS should already be HIGH
  this->set_cs1_high_();  // CS1 HIGH at end

  ESP_LOGD(TAG, "Data transfer complete: %u bytes sent", bytes_written);
  return true;
}

void EPaperT133A01::refresh_screen(bool partial) {
  ESP_LOGD(TAG, "Triggering display refresh");
  this->set_cs1_low_();
  this->cmd_data(REG_DRF, DRF_V, sizeof(DRF_V));  // Display Refresh command (0x12) with data 0x01
  this->wait_busy_();                             // Wait for refresh to complete
  this->set_cs1_high_();
  delay(30);  // Delay after CS1 high as per manufacturer's macro
}

void EPaperT133A01::power_on() {
  // Send Power On command (0x04) as per manufacturer's EPD_UPDATE sequence
  // This must be sent before the DRF (Display Refresh) command
  ESP_LOGD(TAG, "Powering on display");
  this->set_cs1_low_();
  this->command(REG_PON);  // Power On command (0x04)
  this->wait_busy_();      // Wait for power-on sequence to complete
  this->set_cs1_high_();
  delay(30);  // Delay after CS1 high as per manufacturer's macro
  ESP_LOGV(TAG, "Power on complete");
}

void EPaperT133A01::power_off() {
  ESP_LOGD(TAG, "Powering off display");
  this->set_cs1_low_();
  this->cmd_data(REG_POF, POF_V, sizeof(POF_V));  // Power Off command (0x02) with data 0x00
  this->wait_busy_();                             // Wait for power-off sequence to complete
  this->set_cs1_high_();
  delay(30);  // Delay after CS1 high as per manufacturer's macro
  ESP_LOGV(TAG, "Power off complete");
}

void EPaperT133A01::deep_sleep() {
  ESP_LOGD(TAG, "Entering deep sleep");
  this->set_cs1_low_();
  this->cmd_data(REG_SLEEP, SLEEP_V, sizeof(SLEEP_V));  // Sleep command (0x07) with data 0xA5
  this->set_cs1_high_();
  ESP_LOGV(TAG, "Deep sleep mode active");
}

}  // namespace esphome::epaper_spi
