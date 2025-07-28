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

  // Clear UART buffer
  while (this->available()) {
    uint8_t dummy;
    this->read_byte(&dummy);
  }

  // Set mode first before waking up
  if (this->mode_ == MODE_QA) {
    ESP_LOGCONFIG(TAG, "Setting ZH03B to Q&A mode");
    this->send_command_(CMD_SET_QA_MODE, 9);
    delay(500);
    // Put to dormant after mode change
    this->send_command_(CMD_DORMANT_ON, 9);
    delay(500);
  } else {
    ESP_LOGCONFIG(TAG, "Setting ZH03B to Passive mode");
    this->send_command_(CMD_SET_PASSIVE_MODE, 9);
    delay(500);
    // Wake up for continuous reading in passive mode
    this->send_command_(CMD_DORMANT_OFF, 9);
    delay(1000);
  }

  ESP_LOGCONFIG(TAG, "Waiting 30s for sensor stabilization...");
}

void ZH03BSensor::loop() {
  // Stabilization period
  if (this->stabilizing_) {
    if (millis() - this->stabilization_start_ < 30000) {
      // During stabilization, clear buffer to prevent old data
      while (this->available()) {
        uint8_t dummy;
        this->read_byte(&dummy);
      }
      return;
    }
    this->stabilizing_ = false;
    ESP_LOGI(TAG, "Sensor stabilization complete");
  }

  if (this->mode_ == MODE_QA) {
    // Q&A Mode State Machine
    switch (qa_state_) {
      case QA_IDLE:
        if (millis() - this->last_request_ >= this->update_interval_) {
          ESP_LOGD(TAG, "QA: Waking sensor");
          this->send_command_(CMD_DORMANT_OFF, 9);
          qa_timer_ = millis();
          qa_state_ = QA_WAKE_SENT;
          this->qa_index_ = 0;  // Reset buffer
        }
        break;

      case QA_WAKE_SENT:
        if (millis() - qa_timer_ >= 10000) {  // 10s wake time
          ESP_LOGD(TAG, "QA: Requesting data");
          // Clear buffer before request
          while (this->available()) {
            uint8_t dummy;
            this->read_byte(&dummy);
          }
          this->send_command_(CMD_READ_DATA, 9);
          qa_timer_ = millis();
          qa_state_ = QA_READ_SENT;
          this->qa_index_ = 0;
        }
        break;

      case QA_READ_SENT:
        // Read available data
        while (this->available() && this->qa_index_ < 9) {
          uint8_t byte;
          this->read_byte(&byte);
          
          // Sync to start byte
          if (this->qa_index_ == 0 && byte != 0xFF) {
            continue;  // Skip until we find start byte
          }
          
          this->qa_buffer_[this->qa_index_++] = byte;
          
          // Check if we have complete packet
          if (this->qa_index_ == 9) {
            if (this->qa_buffer_[0] == 0xFF && this->qa_buffer_[1] == 0x86) {
              if (this->validate_checksum_qa_(this->qa_buffer_)) {
                this->parse_qa_data_(this->qa_buffer_);
                ESP_LOGD(TAG, "QA: Data received successfully");
              } else {
                ESP_LOGW(TAG, "QA: Checksum failed");
              }
            } else {
              ESP_LOGW(TAG, "QA: Invalid response header");
            }
            qa_timer_ = millis();
            qa_state_ = QA_WAITING_SLEEP;
          }
        }
        
        // Timeout check
        if (millis() - qa_timer_ >= 3000) {  // 3s timeout
          ESP_LOGW(TAG, "QA: Response timeout");
          qa_timer_ = millis();
          qa_state_ = QA_WAITING_SLEEP;
        }
        break;

      case QA_WAITING_SLEEP:
        if (millis() - qa_timer_ >= 2000) {  // 2s delay before sleep
          ESP_LOGD(TAG, "QA: Putting sensor to sleep");
          this->send_command_(CMD_DORMANT_ON, 9);
          this->last_request_ = millis();
          qa_state_ = QA_IDLE;
        }
        break;
    }
    return;
  }

  // Passive (Initiative) Mode
  while (this->available()) {
    uint8_t byte;
    this->read_byte(&byte);
    
    // Sync to header
    if (this->initiative_index_ == 0 && byte != 0x42) {
      continue;  // Skip until we find first header byte
    }
    if (this->initiative_index_ == 1 && byte != 0x4D) {
      this->initiative_index_ = 0;  // Reset if second header byte wrong
      continue;
    }
    
    this->initiative_buffer_[this->initiative_index_++] = byte;
    
    // Check if we have complete packet
    if (this->initiative_index_ >= 24) {
      // Validate packet
      if (this->initiative_buffer_[0] == 0x42 && this->initiative_buffer_[1] == 0x4D) {
        if (this->validate_checksum_initiative_(this->initiative_buffer_)) {
          this->parse_initiative_data_(this->initiative_buffer_);
        } else {
          ESP_LOGW(TAG, "Passive: Checksum mismatch");
        }
      }
      this->initiative_index_ = 0;  // Reset for next packet
    }
  }
}

void ZH03BSensor::parse_initiative_data_(const uint8_t *data) {
  // Log raw data for debugging
  ESP_LOGV(TAG, "Passive raw data: %02X %02X ... PM2.5 bytes: %02X %02X", 
           data[0], data[1], data[12], data[13]);

  uint16_t pm_1_0  = (data[10] << 8) | data[11];
  uint16_t pm_2_5  = (data[12] << 8) | data[13];
  uint16_t pm_10_0 = (data[14] << 8) | data[15];

  ESP_LOGD(TAG, "Passive mode - PM1.0: %u, PM2.5: %u, PM10: %u µg/m³", 
           pm_1_0, pm_2_5, pm_10_0);

  // Validate reasonable values
  if (pm_1_0 > 1000 || pm_2_5 > 1000 || pm_10_0 > 1000) {
    ESP_LOGW(TAG, "Unrealistic PM values, ignoring");
    return;
  }

  // Update sensors
  if (this->pm_1_0_sensor_ != nullptr)  this->pm_1_0_sensor_->publish_state(pm_1_0);
  if (this->pm_2_5_sensor_ != nullptr)  this->pm_2_5_sensor_->publish_state(pm_2_5);
  if (this->pm_10_0_sensor_ != nullptr) this->pm_10_0_sensor_->publish_state(pm_10_0);
  
  this->last_transmission_ = millis();
}

void ZH03BSensor::parse_qa_data_(const uint8_t *data) {
  // Log raw data for debugging
  ESP_LOGV(TAG, "QA raw data: %02X %02X %02X %02X %02X %02X %02X %02X %02X",
           data[0], data[1], data[2], data[3], data[4], 
           data[5], data[6], data[7], data[8]);
  
  // Parse PM values from correct positions (matching YAML)
  uint16_t pm_2_5  = (data[2] << 8) | data[3];
  uint16_t pm_10_0 = (data[4] << 8) | data[5];
  uint16_t pm_1_0  = (data[6] << 8) | data[7];
  
  ESP_LOGD(TAG, "Q&A mode - PM1.0: %u, PM2.5: %u, PM10: %u µg/m³", 
           pm_1_0, pm_2_5, pm_10_0);
  
  // Validate reasonable values
  if (pm_1_0 > 1000 || pm_2_5 > 1000 || pm_10_0 > 1000) {
    ESP_LOGW(TAG, "Unrealistic PM values, ignoring");
    return;
  }
  
  // Update sensors
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
  return (sum == checksum);
}

bool ZH03BSensor::validate_checksum_qa_(const uint8_t *data) {
  uint8_t sum = 0;
  for (int i = 1; i < 8; i++) {
    sum += data[i];
  }
  uint8_t calculated = (~sum) + 1;
  return (calculated == data[8]);
}

void ZH03BSensor::send_command_(const uint8_t *command, uint8_t length) {
  this->write_array(command, length);
  this->flush();
  ESP_LOGV(TAG, "Command sent: %02X %02X %02X ... %02X", 
           command[0], command[1], command[2], command[8]);
}

void ZH03BSensor::dump_config() {
  ESP_LOGCONFIG(TAG, "ZH03B Particulate Matter Sensor:");
  ESP_LOGCONFIG(TAG, "  Mode: %s", this->mode_ == MODE_QA ? "Q&A" : "Passive");
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
