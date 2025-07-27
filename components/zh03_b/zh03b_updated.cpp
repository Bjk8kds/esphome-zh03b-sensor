#include "zh03b.h"
#include "esphome/core/log.h"

namespace esphome {
namespace zh03b {

static const char *const TAG = "zh03b";

// Q&A mode commands (9 bytes each)
const uint8_t ZH03BSensor::CMD_READ_DATA[9] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
const uint8_t ZH03BSensor::CMD_SET_QA_MODE[9] = {0xFF, 0x01, 0x78, 0x41, 0x00, 0x00, 0x00, 0x00, 0x46};
const uint8_t ZH03BSensor::CMD_SET_PASSIVE_MODE[9] = {0xFF, 0x01, 0x78, 0x40, 0x00, 0x00, 0x00, 0x00, 0x47};
const uint8_t ZH03BSensor::CMD_DORMANT_ON[9]  = {0xFF, 0x01, 0xA7, 0x01, 0x00, 0x00, 0x00, 0x00, 0x57};
const uint8_t ZH03BSensor::CMD_DORMANT_OFF[9] = {0xFF, 0x01, 0xA7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58};

void ZH03BSensor::setup() {
  ESP_LOGCONFIG(TAG, "Setting up ZH03B...");
  this->initiative_index_ = 0;
  this->qa_index_ = 0;
  this->waiting_for_response_ = false;
  this->stabilizing_ = true;
  this->stabilization_start_ = millis();

  while (this->available()) {
    uint8_t dummy;
    this->read_byte(&dummy);
  }

  delay(500);

  // Wake up sensor dari dormant
  ESP_LOGD(TAG, "Sending wake up command");
  this->send_command_(CMD_DORMANT_OFF, 9);
  delay(1000);

  // Set mode
  if (this->mode_ == MODE_QA) {
    ESP_LOGCONFIG(TAG, "Setting ZH03B to Q&A mode");
    this->set_qa_mode();
  } else {
    ESP_LOGCONFIG(TAG, "Using default passive mode (initiative upload)");
    this->set_passive_mode();
  }

  // Force sensor untuk mulai ulang pengukuran
  ESP_LOGD(TAG, "Forcing sensor to start fresh measurement");
  this->send_command_(CMD_DORMANT_ON, 9);
  delay(100);
  this->send_command_(CMD_DORMANT_OFF, 9);
  delay(1000);

  ESP_LOGCONFIG(TAG, "Waiting 30s for sensor stabilization...");
}

void ZH03BSensor::loop() {
  if (this->stabilizing_) {
    if (millis() - this->stabilization_start_ < 30000)
      return;
    this->stabilizing_ = false;
  }

  if (this->mode_ == MODE_QA) {
    switch (qa_state_) {
      case QA_IDLE:
        if (millis() - this->last_request_ >= this->update_interval_) {
          ESP_LOGD(TAG, "QA: Sending DORMANT_OFF (wake sensor)");
          this->send_command_(CMD_DORMANT_OFF, 9);
          qa_timer_ = millis();
          qa_state_ = QA_WAKE_SENT;
        }
        break;

      case QA_WAKE_SENT:
        if (millis() - qa_timer_ >= 10000) {  // Wait 10s
          ESP_LOGD(TAG, "QA: Sending READ_DATA");
          this->send_command_(CMD_READ_DATA, 9);
          qa_timer_ = millis();
          qa_state_ = QA_READ_SENT;
        }
        break;

      case QA_READ_SENT:
        if (this->available()) {
          while (this->available()) {
            uint8_t byte;
            this->read_byte(&byte);
            this->qa_buffer_[this->qa_index_++] = byte;
            if (this->qa_index_ >= 9) {
              if (this->qa_buffer_[0] == 0xFF && this->qa_buffer_[1] == 0x86) {
                uint8_t sum = 0;
                for (int i = 1; i < 8; i++) sum += this->qa_buffer_[i];
                uint8_t chk = (~sum) + 1;
                if (chk == this->qa_buffer_[8]) {
                  this->parse_qa_data_(this->qa_buffer_);
                } else {
                  ESP_LOGW(TAG, "QA: Checksum mismatch");
                }
              }
              this->qa_index_ = 0;
              qa_timer_ = millis();
              qa_state_ = QA_WAITING_SLEEP;
            }
          }
        } else if (millis() - qa_timer_ >= 2000) {
          ESP_LOGW(TAG, "QA: No response from sensor");
          this->qa_index_ = 0;
          qa_timer_ = millis();
          qa_state_ = QA_WAITING_SLEEP;
        }
        break;

      case QA_WAITING_SLEEP:
        if (millis() - qa_timer_ >= 1000) {
          ESP_LOGD(TAG, "QA: Sending DORMANT_ON (put sensor to sleep)");
          this->send_command_(CMD_DORMANT_ON, 9);
          this->last_request_ = millis();
          qa_state_ = QA_IDLE;
        }
        break;
    }
    return;
  }

  // Passive mode logic tetap
  while (this->available()) {
    uint8_t byte;
    this->read_byte(&byte);
    this->initiative_buffer_[this->initiative_index_++] = byte;
    if (this->initiative_index_ == 24) {
      this->initiative_index_ = 0;
      if (this->initiative_buffer_[0] == 0x42 && this->initiative_buffer_[1] == 0x4D) {
        uint16_t sum = 0;
        for (int i = 0; i < 22; i++) sum += this->initiative_buffer_[i];
        uint16_t checksum = (this->initiative_buffer_[22] << 8) | this->initiative_buffer_[23];
        if (checksum == sum) {
          this->parse_initiative_data_(this->initiative_buffer_);
        } else {
          ESP_LOGW(TAG, "Passive: Checksum mismatch");
        }
      }
    }
  }
}


void ZH03BSensor::parse_initiative_data_(const uint8_t *data) {
  static uint16_t last_pm25 = 0;
  static int same_count = 0;

  uint16_t pm_1_0  = (data[10] << 8) | data[11];
  uint16_t pm_2_5  = (data[12] << 8) | data[13];
  uint16_t pm_10_0 = (data[14] << 8) | data[15];

  if (pm_2_5 == last_pm25) {
    same_count++;
    if (same_count > 60) {
      ESP_LOGW(TAG, "PM2.5 stuck at %u for %d readings!", pm_2_5, same_count);
    }
  } else {
    if (same_count > 10) {
      ESP_LOGI(TAG, "PM2.5 changed from %u to %u after %d readings", last_pm25, pm_2_5, same_count);
    }
    same_count = 0;
    last_pm25 = pm_2_5;
  }

  ESP_LOGD(TAG, "Initiative mode - PM1.0: %u, PM2.5: %u, PM10: %u µg/m³", pm_1_0, pm_2_5, pm_10_0);

  if (pm_1_0 > 1000 || pm_2_5 > 1000 || pm_10_0 > 1000) {
    ESP_LOGW(TAG, "Unrealistic PM values detected, ignoring");
    return;
  }

  if (this->pm_1_0_sensor_ != nullptr)  this->pm_1_0_sensor_->publish_state(pm_1_0);
  if (this->pm_2_5_sensor_ != nullptr)  this->pm_2_5_sensor_->publish_state(pm_2_5);
  if (this->pm_10_0_sensor_ != nullptr) this->pm_10_0_sensor_->publish_state(pm_10_0);
}

void ZH03BSensor::parse_qa_data_(const uint8_t *data) {
  // Check if this is really a data response
  if (data[0] != 0xFF || data[1] != 0x86) {
    ESP_LOGW(TAG, "Invalid Q&A data response header");
    return;
  }
  
  // Parse PM values from correct positions
  uint16_t pm_2_5  = (data[2] << 8) | data[3];
  uint16_t pm_10_0 = (data[4] << 8) | data[5];
  uint16_t pm_1_0  = (data[6] << 8) | data[7];
  
  ESP_LOGD(TAG, "Q&A mode - PM1.0: %u, PM2.5: %u, PM10: %u µg/m³", pm_1_0, pm_2_5, pm_10_0);
  
  // Additional validation for suspicious values
  if (pm_1_0 == 256 || pm_2_5 == 256 || pm_10_0 == 256) {
    ESP_LOGW(TAG, "Suspicious value 256 detected, possible parsing error");
    return;
  }
  
  // Validate reasonable values
  if (pm_1_0 > 1000 || pm_2_5 > 1000 || pm_10_0 > 1000) {
    ESP_LOGW(TAG, "Unrealistic PM values detected, ignoring");
    return;
  }
  
  if (this->pm_1_0_sensor_ != nullptr)  this->pm_1_0_sensor_->publish_state(pm_1_0);
  if (this->pm_2_5_sensor_ != nullptr)  this->pm_2_5_sensor_->publish_state(pm_2_5);
  if (this->pm_10_0_sensor_ != nullptr) this->pm_10_0_sensor_->publish_state(pm_10_0);
}

bool ZH03BSensor::validate_checksum_initiative_(const uint8_t *data) {
  uint16_t sum = 0;
  for (int i = 0; i < 22; i++) {
    sum += data[i];
  }
  uint16_t checksum = (data[22] << 8) | data[23];
  bool valid = (sum == checksum);
  if (!valid) {
    ESP_LOGV(TAG, "Checksum mismatch: calculated=%04X, received=%04X", sum, checksum);
  }
  return valid;
}

bool ZH03BSensor::validate_checksum_qa_(const uint8_t *data) {
  uint8_t sum = 0;
  for (int i = 1; i < 8; i++) {
    sum += data[i];
  }
  uint8_t calculated = (~sum) + 1;
  bool valid = (calculated == data[8]);
  if (!valid) {
    ESP_LOGV(TAG, "Q&A checksum mismatch: calculated=%02X, received=%02X", calculated, data[8]);
  }
  return valid;
}

void ZH03BSensor::request_reading() {
  if (this->waiting_for_response_) {
    ESP_LOGD(TAG, "Still waiting for response, skipping request");
    return;
  }
  
  // Clear any pending data in UART buffer before sending request
  int cleared = 0;
  while (this->available()) {
    uint8_t dummy;
    this->read_byte(&dummy);
    cleared++;
  }
  if (cleared > 0) {
    ESP_LOGV(TAG, "Cleared %d bytes from UART buffer", cleared);
  }
  
  ESP_LOGD(TAG, "Sending Q&A read request");
  this->send_command_(CMD_READ_DATA, 9);
  this->waiting_for_response_ = true;
  this->qa_index_ = 0;
  
  // Small delay to let sensor process
  delay(10);
}

void ZH03BSensor::set_qa_mode() {
  ESP_LOGD(TAG, "Setting sensor to Q&A mode");
  this->send_command_(CMD_SET_QA_MODE, 9);
  delay(200);  // Give sensor time to process
  this->mode_ = MODE_QA;
  // Clear buffer after mode change
  while (this->available()) {
    uint8_t dummy;
    this->read_byte(&dummy);
  }
}

void ZH03BSensor::set_passive_mode() {
  ESP_LOGD(TAG, "Setting sensor to passive mode");
  this->send_command_(CMD_SET_PASSIVE_MODE, 9);
  delay(200);  // Give sensor time to process
  this->mode_ = MODE_PASSIVE;
  // Clear buffer after mode change
  while (this->available()) {
    uint8_t dummy;
    this->read_byte(&dummy);
  }
}

void ZH03BSensor::send_command_(const uint8_t *command, uint8_t length) {
  this->write_array(command, length);
  this->flush();  // Ensure data is sent
  ESP_LOGV(TAG, "Sent %d bytes command: %02X %02X %02X %02X %02X %02X %02X %02X %02X", 
           length, command[0], command[1], command[2], command[3], 
           command[4], command[5], command[6], command[7], command[8]);
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
