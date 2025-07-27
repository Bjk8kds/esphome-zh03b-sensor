#include "zh03b.h"
#include "esphome/core/log.h"

namespace esphome {
namespace zh03b {

static const char *const TAG = "zh03b";

// Q&A mode commands (9 bytes each)
const uint8_t ZH03BSensor::CMD_READ_DATA[9]      = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
const uint8_t ZH03BSensor::CMD_SET_QA_MODE[9]    = {0xFF, 0x01, 0x78, 0x41, 0x00, 0x00, 0x00, 0x00, 0x46};
const uint8_t ZH03BSensor::CMD_SET_PASSIVE_MODE[9]= {0xFF, 0x01, 0x78, 0x40, 0x00, 0x00, 0x00, 0x00, 0x47};
const uint8_t ZH03BSensor::CMD_DORMANT_ON[9]     = {0xFF, 0x01, 0xA7, 0x01, 0x00, 0x00, 0x00, 0x00, 0x57};
const uint8_t ZH03BSensor::CMD_DORMANT_OFF[9]    = {0xFF, 0x01, 0xA7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58};

void ZH03BSensor::setup() {
  ESP_LOGCONFIG(TAG, "Setting up ZH03B...");
  this->initiative_index_ = 0;
  this->qa_index_ = 0;
  this->waiting_for_response_ = false;

  // Clear UART buffer
  while (this->available()) {
    uint8_t dummy;
    this->read_byte(&dummy);
  }

  // Set mode
  if (this->mode_ == MODE_QA) {
    ESP_LOGCONFIG(TAG, "Setting ZH03B to Q&A mode");
    this->set_qa_mode();
  } else {
    ESP_LOGCONFIG(TAG, "Using default passive mode (initiative upload)");
  }
}

void ZH03BSensor::loop() {
  const uint32_t now = millis();

  // Untuk mode Q&A, request berkala
  if (this->mode_ == MODE_QA) {
    if (now - this->last_request_ >= this->update_interval_) {
      this->request_reading();
      this->last_request_ = now;
    }
  }

  // Baca data
  while (this->available()) {
    uint8_t byte;
    this->read_byte(&byte);

    if (this->mode_ == MODE_PASSIVE) {
      // Initiative mode (24 bytes)
      if (this->initiative_index_ == 0 && byte != HEADER_BYTE_1) continue;
      if (this->initiative_index_ == 1 && byte != HEADER_BYTE_2) {
        this->initiative_index_ = 0;
        continue;
      }
      this->initiative_buffer_[this->initiative_index_++] = byte;
      if (this->initiative_index_ == 24) {
        if (this->validate_checksum_initiative_(this->initiative_buffer_)) {
          this->parse_initiative_data_(this->initiative_buffer_);
          this->last_transmission_ = now;
        } else {
          ESP_LOGW(TAG, "Invalid checksum in initiative mode");
        }
        this->initiative_index_ = 0;
      }
    } else {
      // Q&A mode (9 bytes)
      if (this->qa_index_ == 0 && byte != QA_START_BYTE) continue;
      this->qa_buffer_[this->qa_index_++] = byte;
      if (this->qa_index_ == 9) {
        if (this->validate_checksum_qa_(this->qa_buffer_)) {
          this->parse_qa_data_(this->qa_buffer_);
          this->last_transmission_ = now;
          this->waiting_for_response_ = false;
        } else {
          ESP_LOGW(TAG, "Invalid checksum in Q&A mode");
        }
        this->qa_index_ = 0;
      }
    }
  }

  // Q&A timeout
  if (this->mode_ == MODE_QA && this->waiting_for_response_) {
    if (now - this->last_request_ > 2000) {
      ESP_LOGW(TAG, "Timeout waiting for Q&A response");
      this->waiting_for_response_ = false;
      this->qa_index_ = 0;
    }
  }
}

void ZH03BSensor::parse_initiative_data_(const uint8_t *data) {
  uint16_t pm_1_0  = (data[11] << 8) | data[12];
  uint16_t pm_2_5  = (data[13] << 8) | data[14];
  uint16_t pm_10_0 = (data[15] << 8) | data[16];
  ESP_LOGD(TAG, "Initiative mode - PM1.0: %u, PM2.5: %u, PM10: %u", pm_1_0, pm_2_5, pm_10_0);
  if (this->pm_1_0_sensor_ != nullptr)  this->pm_1_0_sensor_->publish_state(pm_1_0);
  if (this->pm_2_5_sensor_ != nullptr)  this->pm_2_5_sensor_->publish_state(pm_2_5);
  if (this->pm_10_0_sensor_ != nullptr) this->pm_10_0_sensor_->publish_state(pm_10_0);
}

void ZH03BSensor::parse_qa_data_(const uint8_t *data) {
  uint16_t pm_2_5  = (data[2] << 8) | data[3];
  uint16_t pm_10_0 = (data[4] << 8) | data[5];
  uint16_t pm_1_0  = (data[6] << 8) | data[7];
  ESP_LOGD(TAG, "Q&A mode - PM1.0: %u, PM2.5: %u, PM10: %u", pm_1_0, pm_2_5, pm_10_0);
  if (this->pm_1_0_sensor_ != nullptr)  this->pm_1_0_sensor_->publish_state(pm_1_0);
  if (this->pm_2_5_sensor_ != nullptr)  this->pm_2_5_sensor_->publish_state(pm_2_5);
  if (this->pm_10_0_sensor_ != nullptr) this->pm_10_0_sensor_->publish_state(pm_10_0);
}

bool ZH03BSensor::validate_checksum_initiative_(const uint8_t *data) {
  uint16_t sum = 0;
  for (int i = 0; i < 22; i++) sum += data[i];
  uint16_t checksum = (data[22] << 8) | data[23];
  return sum == checksum;
}

bool ZH03BSensor::validate_checksum_qa_(const uint8_t *data) {
  uint8_t sum = 0;
  for (int i = 1; i < 8; i++) sum += data[i];
  uint8_t calculated = (~sum) + 1;
  return calculated == data[8];
}

void ZH03BSensor::request_reading() {
  if (this->waiting_for_response_) {
    ESP_LOGD(TAG, "Still waiting for response, skipping request");
    return;
  }
  ESP_LOGD(TAG, "Requesting Q&A reading");
  this->send_command_(CMD_READ_DATA, 9);
  this->waiting_for_response_ = true;
  this->qa_index_ = 0;
}

void ZH03BSensor::set_qa_mode() {
  ESP_LOGD(TAG, "Setting sensor to Q&A mode");
  this->send_command_(CMD_SET_QA_MODE, 9);
  delay(100);
  this->mode_ = MODE_QA;
}

void ZH03BSensor::set_passive_mode() {
  ESP_LOGD(TAG, "Setting sensor to passive mode");
  this->send_command_(CMD_SET_PASSIVE_MODE, 9);
  delay(100);
  this->mode_ = MODE_PASSIVE;
}

void ZH03BSensor::set_dormant_mode(bool dormant) {
  if (dormant) {
    ESP_LOGD(TAG, "Entering dormant mode");
    this->send_command_(CMD_DORMANT_ON, 9);
  } else {
    ESP_LOGD(TAG, "Exiting dormant mode");
    this->send_command_(CMD_DORMANT_OFF, 9);
  }
  delay(100);
}

void ZH03BSensor::send_command_(const uint8_t *command, uint8_t length) {
  this->write_array(command, length);
  ESP_LOGV(TAG, "Sent %d bytes command", length);
}

void ZH03BSensor::dump_config() {
  ESP_LOGCONFIG(TAG, "ZH03B Particulate Matter Sensor:");
  ESP_LOGCONFIG(TAG, "  Mode: %s", this->mode_ == MODE_QA ? "Q&A" : "Passive (Initiative)");
  ESP_LOGCONFIG(TAG, "  Update Interval: %u ms", this->update_interval_);
  LOG_SENSOR("  ", "PM1.0", this->pm_1_0_sensor_);
  LOG_SENSOR("  ", "PM2.5", this->pm_2_5_sensor_);
  LOG_SENSOR("  ", "PM10.0", this->pm_10_0_sensor_);
  this->check_uart_settings(9600);
}

float ZH03BSensor::get_setup_priority() const { 
  return setup_priority::DATA; 
}

}  // namespace zh03b
}  // namespace esphome
