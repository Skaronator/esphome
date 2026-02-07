#include "epaper_spi_t133a01.h"

#include <algorithm>

#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace esphome::epaper_spi {

static constexpr const char *const TAG = "epaper_spi.t133a01";
static constexpr uint8_t GRAY_THRESHOLD = 50;

// --- T133A01 controller register/command constants ---
static constexpr uint8_t R00_PSR = 0x00;
static constexpr uint8_t R01_PWR = 0x01;
static constexpr uint8_t R02_POF = 0x02;
static constexpr uint8_t R04_PON = 0x04;
static constexpr uint8_t R05_BTST_N = 0x05;
static constexpr uint8_t R06_BTST_P = 0x06;
static constexpr uint8_t R10_DTM = 0x10;
static constexpr uint8_t R12_DRF = 0x12;
static constexpr uint8_t R50_CDI = 0x50;
static constexpr uint8_t R61_TRES = 0x61;
static constexpr uint8_t RE0_CCSET = 0xE0;
static constexpr uint8_t RE3_PWS = 0xE3;

static constexpr uint8_t PSR_V[] = {0xDF, 0x69};
static constexpr uint8_t PWR_V[] = {0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38};
static constexpr uint8_t POF_V[] = {0x00};
static constexpr uint8_t DRF_V[] = {0x01};
static constexpr uint8_t CDI_V[] = {0x37};
static constexpr uint8_t TRES_V[] = {0x04, 0xB0, 0x03, 0x20};
static constexpr uint8_t PWS_V[] = {0x22};
static constexpr uint8_t BTST_P_V[] = {0xD8, 0x18};
static constexpr uint8_t BTST_N_V[] = {0xD8, 0x18};
static constexpr uint8_t SLEEP_V[] = {0xA5};

static constexpr uint8_t R74_DATA[] = {0xC0, 0x1C, 0x1C, 0xCC, 0xCC, 0xCC, 0x15, 0x15, 0x55};
static constexpr uint8_t RF0_DATA[] = {0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};
static constexpr uint8_t R60_DATA[] = {0x03, 0x03};
static constexpr uint8_t R86_DATA[] = {0x10};
static constexpr uint8_t RB6_DATA[] = {0x07};
static constexpr uint8_t RB7_DATA[] = {0x01};
static constexpr uint8_t RB0_DATA[] = {0x01};
static constexpr uint8_t RB1_DATA[] = {0x02};

static constexpr uint8_t CCSET_V_CUR[] = {0x01};

// Default palette indices used by the manufacturer library in 6-color mode
static constexpr uint8_t TFT_WHITE = 0x0;
static constexpr uint8_t TFT_GREEN = 0x2;
static constexpr uint8_t TFT_RED = 0x6;
static constexpr uint8_t TFT_YELLOW = 0xB;
static constexpr uint8_t TFT_BLUE = 0xD;
static constexpr uint8_t TFT_BLACK = 0xF;

static uint8_t color_to_palette(Color color) {
  unsigned char max_rgb = std::max({color.r, color.g, color.b});
  unsigned char min_rgb = std::min({color.r, color.g, color.b});

  if ((max_rgb - min_rgb) < GRAY_THRESHOLD) {
    if ((static_cast<int>(color.r) + color.g + color.b) > 382) {
      return TFT_WHITE;
    }
    return TFT_BLACK;
  }

  bool r_on = (color.r > 128);
  bool g_on = (color.g > 128);
  bool b_on = (color.b > 128);

  if (r_on && g_on && !b_on) {
    return TFT_YELLOW;
  }
  if (r_on && !g_on && !b_on) {
    return TFT_RED;
  }
  if (!r_on && g_on && !b_on) {
    return TFT_GREEN;
  }
  if (!r_on && !g_on && b_on) {
    return TFT_BLUE;
  }
  if (!r_on && g_on && b_on) {
    // Cyan -> closest is Green
    return TFT_GREEN;
  }
  if (r_on && !g_on) {
    // Magenta -> closest is Red
    return TFT_RED;
  }
  if (r_on) {
    return TFT_WHITE;
  }
  return TFT_BLACK;
}

static constexpr uint8_t color_get(uint8_t nibble) {
  // Mapping from manufacturer library (T133A01_Defines.h)
  // nibble is one pixel in the Seeed palette (0..15)
  switch (nibble & 0x0F) {
    case 0x0F:
      return 0x00;  // black
    case 0x00:
      return 0x01;  // white
    case 0x02:
      return 0x06;  // green
    case 0x0B:
      return 0x02;  // yellow
    case 0x0D:
      return 0x05;  // blue
    case 0x06:
      return 0x03;  // red
    default:
      return 0x01;  // map unknown to white
  }
}

void EPaperT133A01::setup() {
  ESP_LOGI(TAG, "T133A01 driver marker: async-update v2");
  EPaperBase::setup();
  if (this->is_failed())
    return;

  if (this->cs1_pin_ == nullptr) {
    this->mark_failed(LOG_STR("'cs1_pin' is required for T133A01"));
    return;
  }

  // Ensure CS1 is inactive before registering the second SPI device
  this->cs1_pin_->setup();
  this->cs1_pin_->digital_write(true);

  this->cs1_device_.set_spi_parent(this->parent_);
  this->cs1_device_.set_cs_pin(this->cs1_pin_);
  this->cs1_device_.set_data_rate(this->data_rate_);
  this->cs1_device_.set_bit_order(this->bit_order_);
  this->cs1_device_.set_mode(this->mode_);
  this->cs1_device_.set_write_only(true);
  this->cs1_device_.spi_setup();
}

void EPaperT133A01::wait_for_idle_sync_() const {
  if (this->busy_pin_ == nullptr)
    return;
  const uint32_t start = millis();
  bool printed_waiting = false;
  while (this->busy_pin_->digital_read()) {
    if (!printed_waiting) {
      ESP_LOGV(TAG, "Waiting for BUSY to clear...");
      printed_waiting = true;
    }
    delay(10);
    if (printed_waiting && (millis() - start) >= 5000 && ((millis() - start) % 1000) < 20) {
      ESP_LOGV(TAG, "Still BUSY after %u ms", (unsigned) (millis() - start));
    }
  }
  if (printed_waiting) {
    ESP_LOGV(TAG, "BUSY cleared after %u ms", (unsigned) (millis() - start));
  }
}

void EPaperT133A01::cs1_command_(uint8_t value) {
  ESP_LOGV(TAG, "CS1 Command: 0x%02X", value);
  this->dc_pin_->digital_write(false);
  this->cs1_device_.enable();
  this->cs1_device_.write_byte(value);
  this->cs1_device_.disable();
}

void EPaperT133A01::cs1_cmd_data_(uint8_t command, const uint8_t *data, size_t length) {
  ESP_LOGV(TAG, "CS1 Cmd: 0x%02X, len=%u", command, (unsigned) length);
  this->dc_pin_->digital_write(false);
  this->cs1_device_.enable();
  this->cs1_device_.write_byte(command);
  if (length > 0) {
    this->dc_pin_->digital_write(true);
    this->cs1_device_.write_array(data, length);
  }
  this->cs1_device_.disable();
}

void EPaperT133A01::dump_config() {
  EPaperBase::dump_config();
  LOG_PIN("  CS1 Pin: ", this->cs1_pin_);
  for (auto *pin : this->enable_pins_) {
    LOG_PIN("  Enable Pin: ", pin);
  }
}

bool EPaperT133A01::reset() {
  if (this->reset_pin_ != nullptr) {
    if (this->state_ == EPaperState::RESET) {
      this->reset_pin_->digital_write(false);
      return false;
    }
    this->reset_pin_->digital_write(true);
    // Manufacturer code waits 20ms after releasing reset
    this->next_delay_ = 20;
  }
  return true;
}

bool EPaperT133A01::initialise(bool partial) {
  (void) partial;

  this->update_phase_ = 0;
  this->refresh_phase_ = 0;
  this->power_off_phase_ = 0;
  this->transfer_prologue_phase_ = 0;

  // Sequence adapted from Seeed_GFX T133A01_Defines.h (EPD_INIT)
  this->wait_for_idle_sync_();

  if (this->busy_pin_ != nullptr) {
    ESP_LOGV(TAG, "BUSY before init: %d", (int) this->busy_pin_->digital_read());
  }

  // 0x74 is sent on CS (primary)
  this->cmd_data(0x74, R74_DATA, sizeof(R74_DATA));

  // Remaining init commands are sent on CS1
  this->cs1_cmd_data_(0xF0, RF0_DATA, sizeof(RF0_DATA));
  delay(10);
  this->cs1_cmd_data_(R00_PSR, PSR_V, sizeof(PSR_V));
  delay(10);
  this->cs1_cmd_data_(R50_CDI, CDI_V, sizeof(CDI_V));
  delay(10);
  this->cs1_cmd_data_(0x60, R60_DATA, sizeof(R60_DATA));
  delay(10);
  this->cs1_cmd_data_(0x86, R86_DATA, sizeof(R86_DATA));
  delay(10);
  this->cs1_cmd_data_(RE3_PWS, PWS_V, sizeof(PWS_V));
  delay(10);
  this->cs1_cmd_data_(R61_TRES, TRES_V, sizeof(TRES_V));
  delay(10);

  // Note: In the manufacturer macro EPD_INIT(), commands after TRES are issued with CS1 deasserted,
  // so they are sent on the primary CS.
  this->cmd_data(R01_PWR, PWR_V, sizeof(PWR_V));
  delay(10);
  this->cmd_data(0xB6, RB6_DATA, sizeof(RB6_DATA));
  delay(10);
  this->cmd_data(R06_BTST_P, BTST_P_V, sizeof(BTST_P_V));
  delay(10);
  this->cmd_data(0xB7, RB7_DATA, sizeof(RB7_DATA));
  delay(10);
  this->cmd_data(R05_BTST_N, BTST_N_V, sizeof(BTST_N_V));
  delay(10);
  this->cmd_data(0xB0, RB0_DATA, sizeof(RB0_DATA));
  delay(10);
  this->cmd_data(0xB1, RB1_DATA, sizeof(RB1_DATA));
  delay(10);

  return true;
}

void EPaperT133A01::power_on() {
  ESP_LOGV(TAG, "Power on");
  if (this->busy_pin_ != nullptr) {
    ESP_LOGV(TAG, "BUSY before PON: %d", (int) this->busy_pin_->digital_read());
  }

  // Legacy one-shot implementation kept for other models; T133A01 uses power_on_async_()
  this->cs1_command_(R04_PON);
  this->next_delay_ = 0;
}

bool EPaperT133A01::power_on_async_() {
  // Vendor EPD_UPDATE(): CS1 low, PON, CHECK_BUSY, CS1 high, delay(30)
  switch (this->update_phase_) {
    case 0:
      ESP_LOGV(TAG, "EPD_UPDATE: PON (holding CS1 until idle)");
      this->dc_pin_->digital_write(false);
      this->cs1_device_.enable();
      this->cs1_device_.write_byte(R04_PON);
      this->wait_for_idle_(true);
      this->update_phase_ = 1;
      return false;  // let EPaperBase wait for idle in this state
    case 1:
      // We are idle now. Apply the vendor delay before DRF.
      this->cs1_device_.disable();
      this->delay_until_ = millis() + 30;
      this->update_phase_ = 2;
      return false;
    default:
      this->update_phase_ = 0;
      return true;
  }
}

void EPaperT133A01::refresh_screen(bool partial) {
  (void) partial;
  ESP_LOGV(TAG, "Refresh");
  if (this->busy_pin_ != nullptr) {
    ESP_LOGV(TAG, "BUSY before DRF: %d", (int) this->busy_pin_->digital_read());
  }

  // Legacy one-shot implementation kept for other models; T133A01 uses refresh_screen_async_()
  this->cs1_cmd_data_(R12_DRF, DRF_V, sizeof(DRF_V));
  this->next_delay_ = 0;
}

bool EPaperT133A01::refresh_screen_async_(bool partial) {
  (void) partial;
  // Vendor EPD_UPDATE(): CS1 low, DRF, CHECK_BUSY, CS1 high, delay(30)
  switch (this->refresh_phase_) {
    case 0:
      ESP_LOGV(TAG, "EPD_UPDATE: DRF (holding CS1 until idle)");
      this->dc_pin_->digital_write(false);
      this->cs1_device_.enable();
      this->cs1_device_.write_byte(R12_DRF);
      this->dc_pin_->digital_write(true);
      this->cs1_device_.write_array(DRF_V, sizeof(DRF_V));
      this->wait_for_idle_(true);
      this->refresh_phase_ = 1;
      return false;  // let EPaperBase wait for idle
    case 1:
      this->cs1_device_.disable();
      this->delay_until_ = millis() + 30;
      this->refresh_phase_ = 2;
      return false;
    default:
      this->refresh_phase_ = 0;
      return true;
  }
}

void EPaperT133A01::power_off() {
  ESP_LOGV(TAG, "Power off");
  if (this->busy_pin_ != nullptr) {
    ESP_LOGV(TAG, "BUSY before POF: %d", (int) this->busy_pin_->digital_read());
  }

  // Legacy one-shot implementation kept for other models; T133A01 uses power_off_async_()
  this->cs1_cmd_data_(R02_POF, POF_V, sizeof(POF_V));
  this->next_delay_ = 30;
}

bool EPaperT133A01::power_off_async_() {
  // Vendor EPD_UPDATE(): CS1 low, POF, CHECK_BUSY, CS1 high, delay(30)
  switch (this->power_off_phase_) {
    case 0:
      ESP_LOGV(TAG, "EPD_UPDATE: POF (holding CS1 until idle)");
      this->dc_pin_->digital_write(false);
      this->cs1_device_.enable();
      this->cs1_device_.write_byte(R02_POF);
      this->dc_pin_->digital_write(true);
      this->cs1_device_.write_array(POF_V, sizeof(POF_V));
      this->wait_for_idle_(true);
      this->power_off_phase_ = 1;
      return false;  // let EPaperBase wait for idle
    case 1:
      this->cs1_device_.disable();
      this->delay_until_ = millis() + 30;
      this->power_off_phase_ = 2;
      return false;
    default:
      this->power_off_phase_ = 0;
      return true;
  }
}

void EPaperT133A01::deep_sleep() {
  ESP_LOGV(TAG, "Deep sleep");
  this->cmd_data(0x07, SLEEP_V, sizeof(SLEEP_V));
}

void EPaperT133A01::fill(Color color) {
  if (this->get_clipping().is_set()) {
    EPaperBase::fill(color);
    return;
  }

  const uint8_t pixel = color_to_palette(color) & 0x0F;
  this->buffer_.fill(pixel | (pixel << 4));
  this->x_high_ = this->width_;
  this->y_high_ = this->height_;
  this->x_low_ = 0;
  this->y_low_ = 0;
}

void EPaperT133A01::clear() { this->fill(COLOR_ON); }

void HOT EPaperT133A01::draw_pixel_at(int x, int y, Color color) {
  if (!this->rotate_coordinates_(x, y))
    return;

  uint8_t pixel_bits = color_to_palette(color) & 0x0F;
  uint32_t pixel_position = x + y * this->get_width_internal();
  uint32_t byte_position = pixel_position / 2;
  uint8_t original = this->buffer_[byte_position];
  if ((pixel_position & 1U) != 0U) {
    this->buffer_[byte_position] = (original & 0xF0) | pixel_bits;
  } else {
    this->buffer_[byte_position] = (original & 0x0F) | (pixel_bits << 4);
  }
}

bool HOT EPaperT133A01::transfer_data() {
  const uint16_t width = this->get_width_internal();
  const uint16_t height = this->get_height_internal();

  // The manufacturer driver pushes the screen in two halves (two chip selects).
  // Each row is (width / 2) bytes in our 4bpp buffer. Each controller consumes half that.
  const uint16_t bytes_per_block_row = width / 4;
  const uint16_t stride = bytes_per_block_row * 2;

  const size_t half_frame_len = static_cast<size_t>(height) * bytes_per_block_row;

  if (!this->transfer_prologue_done_) {
    // Transfer prologue (equivalent to EPD_PUSH_NEW_COLORS preamble) without blocking.
    // Vendor does: CCSET -> CHECK_BUSY -> delay(10)
    if (this->transfer_prologue_phase_ == 0) {
      this->cs1_cmd_data_(RE0_CCSET, CCSET_V_CUR, sizeof(CCSET_V_CUR));
      this->transfer_prologue_phase_ = 1;
      this->wait_for_idle_(true);
      return false;  // EPaperBase will wait for idle before calling again
    }
    if (this->transfer_prologue_phase_ == 1) {
      this->delay_until_ = millis() + 10;
      this->transfer_prologue_phase_ = 2;
      return false;
    }

    this->transfer_index_ = 0;
    this->transfer_on_cs1_ = false;
    this->transfer_dtm_sent_ = false;
    this->transfer_streaming_ = false;
    this->transfer_prologue_done_ = true;
    this->transfer_prologue_phase_ = 0;
  }

  // Manufacturer implementation (EPD_PUSH_NEW_COLORS) streams the entire first half (CS),
  // then the entire second half (CS1). It sends DTM (0x10) once per half and relies on
  // the controller auto-incrementing the write address.
  // Interleaving per-row (or re-sending DTM repeatedly) can result in a blank screen.

  uint8_t bytes_to_send[MAX_TRANSFER_SIZE];

  while (true) {
    // Important: the manufacturer driver keeps CS asserted for the entire (half) frame transfer.
    // Toggling CS between the 0x10 (DTM) command and subsequent data can result in the controller
    // ignoring the data stream and leaving the screen blank.
    if (!this->transfer_dtm_sent_) {
      this->dc_pin_->digital_write(false);
      if (!this->transfer_on_cs1_) {
        this->enable();
        this->write_byte(R10_DTM);
      } else {
        this->cs1_device_.enable();
        this->cs1_device_.write_byte(R10_DTM);
      }
      this->dc_pin_->digital_write(true);
      this->transfer_dtm_sent_ = true;
      this->transfer_streaming_ = true;
    }

    size_t out_idx = 0;

    while (this->transfer_index_ < half_frame_len && out_idx < sizeof(bytes_to_send)) {
      const size_t pos = this->transfer_index_++;
      const uint16_t row = pos / bytes_per_block_row;
      const uint16_t col = pos % bytes_per_block_row;

      const size_t base = (static_cast<size_t>(row) * stride) + (this->transfer_on_cs1_ ? bytes_per_block_row : 0);
      const uint8_t b = this->buffer_[base + col];
      const uint8_t hi = (b >> 4) & 0x0F;
      const uint8_t lo = b & 0x0F;
      bytes_to_send[out_idx++] = static_cast<uint8_t>((color_get(hi) << 4) | color_get(lo));
    }

    if (out_idx > 0) {
      if (!this->transfer_on_cs1_) {
        this->write_array(bytes_to_send, out_idx);
      } else {
        this->cs1_device_.write_array(bytes_to_send, out_idx);
      }
    }

    if (this->transfer_index_ >= half_frame_len) {
      if (!this->transfer_on_cs1_) {
        // Finished first half (CS). Release CS before switching to CS1.
        if (this->transfer_streaming_) {
          this->disable();
        }
        // Switch to the second half (CS1)
        this->transfer_on_cs1_ = true;
        this->transfer_index_ = 0;
        this->transfer_dtm_sent_ = false;
        this->transfer_streaming_ = false;
        continue;  // Continue with CS1 half in this call
      }
      // Finished second half (CS1). Release CS1.
      if (this->transfer_streaming_) {
        this->cs1_device_.disable();
      }
      break;  // Finished CS1 half too
    }
  }

  // Done
  this->transfer_index_ = 0;
  this->transfer_on_cs1_ = false;
  this->transfer_dtm_sent_ = false;
  this->transfer_prologue_done_ = false;
  this->transfer_streaming_ = false;
  return true;
}

}  // namespace esphome::epaper_spi
