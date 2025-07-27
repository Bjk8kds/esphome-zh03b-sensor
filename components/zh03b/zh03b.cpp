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

  delay(500);

  // Set mode
  if (this->mode_ == MODE_QA) {
    ESP_LOGCONFIG(TAG, "Setting ZH03B to Q&A mode");
    this->send_command_(CMD_SET_QA_MODE, 9);
    delay(200);
    
    // Put sensor to dormant mode after setting Q&A mode
    ESP_LOGD(TAG, "Putting sensor to dormant mode");
    this->send_command_(CMD_DORMANT_ON, 9);
    delay(100);
  } else {
    ESP_LOGCONFIG(TAG, "Using default passive mode (initiative upload)");
    this->send_command_(CMD_SET_PASSIVE_MODE, 9);
    delay(200);
  }

  ESP_LOGCONFIG(TAG, "Waiting 30s for sensor stabilization...");
}

void ZH03BSensor::loop() {
  // Wait for stabilization period
  if (this->stabilizing_) {
    if (millis() - this->stabilization_start_ < 30000) {
      return;
    }
    this->stabilizing_ = false;
    ESP_LOGD(TAG, "Stabilization complete");
    
    // For QA mode, ensure sensor is in dormant after stabilization
    if (this->mode_ == MODE_QA) {
      this->qa_state_ = QA_IDLE;
      this->last_request_ = millis();  // Start counting from now
    }
  }

  if (this->mode_ == MODE_QA) {
    switch (qa_state_) {
      case QA_IDLE:
        // Check if it's time for next reading (every 60 seconds)
        if (millis() - this->last_request_ >= this->update_interval_) {
          ESP_LOGD(TAG, "QA: Exit dormant mode (start fan & laser)");
          this->send_command_(CMD_DORMANT_OFF, 9);
          qa_timer_ = millis();
          qa_state_ = QA_WAKE_SENT;
        }
        break;

      case QA_WAKE_SENT:
        // Wait 10 seconds for sensor warm up
        if (millis() - qa_timer_ >= 10000) {
          ESP_LOGD(TAG, "QA: Send read data command");
          this->send_command_(CMD_READ_DATA, 9);
          qa_timer_ = millis();
          qa_state_ = QA_READ_SENT;
          this->qa_index_ = 0;  // Reset buffer index
        }
        break;

      case QA_READ_SENT:
        // Try to read response
        if (this->available()) {
          while (this->available()) {
            uint8_t byte;
            this->read_byte(&byte);
            
            // Store byte in buffer
            this->qa_buffer_[this->qa_index_++] = byte;
            
            // Check if we have complete response (9 bytes)
            if (this->qa_index_ >= 9) {
              // Validate response
              if (this->qa_buffer_[0] == 0xFF && this->qa_buffer_[1] == 0x86) {
                // Calculate checksum
                uint8_t sum = 0;
                for (int i = 1; i < 8; i++) {
                  sum += this->qa_buffer_[i];
                }
                uint8_t chk = (~sum) + 1;
                
                if (chk == this->qa_buffer_[8]) {
                  ESP_LOGD(TAG, "QA: Valid data received");
                  this->parse_qa_data_(this->qa_buffer_);
                } else {
                  ESP_LOGW(TAG, "QA: Checksum mismatch (calculated: %02X, received: %02X)", 
                           chk, this->qa_buffer_[8]);
                }
              } else {
                ESP_LOGW(TAG, "QA: Invalid response header");
              }
              
              // Move to waiting state
              this->qa_index_ = 0;
              qa_timer_ = millis();
              qa_state_ = QA_WAITING_SLEEP;
            }
          }
        } else if (millis() - qa_timer_ >= 2000) {
          // Timeout waiting for response
          ESP_LOGW(TAG, "QA: No response from sensor");
          this->qa_index_ = 0;
          qa_timer_ = millis();
          qa_state_ = QA_WAITING_SLEEP;
        }
        break;

      case QA_WAITING_SLEEP:
        // Wait 2 seconds total from read command (already waited some in READ_SENT)
        if (millis() - qa_timer_ >= 1000) {  // Additional 1 second
          ESP_LOGD(TAG, "QA: Enter dormant mode (stop fan & laser)");
          this->send_command_(CMD_DORMANT_ON, 9);
          this->last_request_ = millis();
          qa_state_ = QA_IDLE;
        }
        break;
    }
    return;
  }

  // Passive mode - unchanged
  while (this->available()) {
    uint8_t byte;
    this->read_byte(&byte);
    
    // Shift buffer and add new byte
    if (this->initiative_index_ < 24) {
      this->initiative_buffer_[this->initiative_index_++] = byte;
    }
    
    // Check if we have complete frame
    if (this->initiative_index_ == 24) {
      // Validate header
      if (this->initiative_buffer_[0] == 0x42 && this->initiative_buffer_[1] == 0x4D) {
        // Calculate checksum
        uint16_t sum = 0;
        for (int i = 0; i < 22; i++) {
          sum += this->initiative_buffer_[i];
        }
        uint16_t checksum = (this->initiative_buffer_[22] << 8) | this->initiative_buffer_[23];
        
        if (checksum == sum) {
          this->parse_initiative_data_(this->initiative_buffer_);
        } else {
          ESP_LOGW(TAG, "Passive: Checksum mismatch");
        }
      }
      
      // Reset buffer
      this->initiative_index_ = 0;
    }
  }
}

void ZH03BSensor::parse_initiative_data_(const uint8_t *data) {
  // PM values at fixed positions for passive mode
  uint16_t pm_1_0  = (data[10] << 8) | data[11];
  uint16_t pm_2_5  = (data[12] << 8) | data[13];
  uint16_t pm_10_0 = (data[14] << 8) | data[15];

  ESP_LOGD(TAG, "Passive mode - PM1.0: %u, PM2.5: %u, PM10: %u µg/m³", 
           pm_1_0, pm_2_5, pm_10_0);

  // Validate reasonable values
  if (pm_1_0 > 1000 || pm_2_5 > 1000 || pm_10_0 > 1000) {
    ESP_LOGW(TAG, "Unrealistic PM values detected, ignoring");
    return;
  }

  // Publish states
  if (this->pm_1_0_sensor_ != nullptr)  this->pm_1_0_sensor_->publish_state(pm_1_0);
  if (this->pm_2_5_sensor_ != nullptr)  this->pm_2_5_sensor_->publish_state(pm_2_5);
  if (this->pm_10_0_sensor_ != nullptr) this->pm_10_0_sensor_->publish_state(pm_10_0);
}

void ZH03BSensor::parse_qa_data_(const uint8_t *data) {
  // Validate header
  if (data[0] != 0xFF || data[1] != 0x86) {
    ESP_LOGW(TAG, "Invalid Q&A data response header");
    return;
  }
  
  // Parse PM values - same positions as in YAML
  uint16_t pm_2_5  = (data[2] << 8) | data[3];
  uint16_t pm_10_0 = (data[4] << 8) | data[5];
  uint16_t pm_1_0  = (data[6] << 8) | data[7];
  
  ESP_LOGD(TAG, "Q&A mode - PM1.0: %u, PM2.5: %u, PM10: %u µg/m³", 
           pm_1_0, pm_2_5, pm_10_0);
  
  // Validate reasonable values
  if (pm_1_0 > 1000 || pm_2_5 > 1000 || pm_10_0 > 1000) {
    ESP_LOGW(TAG, "Unrealistic PM values detected, ignoring");
    return;
  }
  
  // Publish states
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

void ZH03BSensor::request_reading() {
  if (this->mode_ != MODE_QA) {
    ESP_LOGW(TAG, "request_reading() only works in Q&A mode");
    return;
  }
  
  ESP_LOGD(TAG, "Manual read request");
  this->send_command_(CMD_READ_DATA, 9);
  this->qa_index_ = 0;
}

void ZH03BSensor::set_qa_mode() {
  ESP_LOGD(TAG, "Setting sensor to Q&A mode");
  this->send_command_(CMD_SET_QA_MODE, 9);
  delay(200);
  this->mode_ = MODE_QA;
  // Clear buffer
  while (this->available()) {
    uint8_t dummy;
    this->read_byte(&dummy);
  }
}

void ZH03BSensor::set_passive_mode() {
  ESP_LOGD(TAG, "Setting sensor to passive mode");
  this->send_command_(CMD_SET_PASSIVE_MODE, 9);
  delay(200);
  this->mode_ = MODE_PASSIVE;
  // Clear buffer
  while (this->available()) {
    uint8_t dummy;
    this->read_byte(&dummy);
  }
}

void ZH03BSensor::send_command_(const uint8_t *command, uint8_t length) {
  this->write_array(command, length);
  this->flush();
  
  ESP_LOGV(TAG, "Sent %d bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X", 
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
