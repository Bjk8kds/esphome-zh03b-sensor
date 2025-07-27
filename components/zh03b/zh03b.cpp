#include "zh03b.h"
#include "esphome/core/log.h"

namespace esphome {
namespace zh03b {

static const char *const TAG = "zh03b";

// Q&A mode commands (9 bytes each)
const uint8_t ZH03BSensor::CMD_READ_DATA[9] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
const uint8_t ZH03BSensor::CMD_SET_QA_MODE[9] = {0xFF, 0x01, 0x78, 0x41, 0x00, 0x00, 0x00, 0x00, 0x46};
const uint8_t ZH03BSensor::CMD_SET_PASSIVE_MODE[9] = {0xFF, 0x01, 0x78, 0x40, 0x00, 0x00, 0x00, 0x00, 0x47};

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

  // Wait a bit before sending commands
  delay(500);

  // Set mode
  if (this->mode_ == MODE_QA) {
    ESP_LOGCONFIG(TAG, "Setting ZH03B to Q&A mode");
    this->set_qa_mode();
  } else {
    ESP_LOGCONFIG(TAG, "Using default passive mode (initiative upload)");
    this->set_passive_mode();
  }
  
  ESP_LOGCONFIG(TAG, "Waiting 30s for sensor stabilization...");
}

void ZH03BSensor::loop() {
  const uint32_t now = millis();
  
  // Debug: Log every 10 seconds
  static uint32_t last_debug_log = 0;
  if (now - last_debug_log > 10000) {
    ESP_LOGD(TAG, "Debug - Mode: %s, Waiting: %s, Initiative Index: %d, QA Index: %d, Last TX: %u ms ago",
             this->mode_ == MODE_QA ? "Q&A" : "Passive",
             this->waiting_for_response_ ? "YES" : "NO",
             this->initiative_index_,
             this->qa_index_,
             now - this->last_transmission_);
    
    // Check if UART has data
    if (this->available()) {
      ESP_LOGD(TAG, "UART has %d bytes available", this->available());
    }
    
    last_debug_log = now;
  }

  // Check if still in stabilization period (30 seconds)
  if (this->stabilizing_) {
    if (now - this->stabilization_start_ < 30000) {
      // Clear any data during stabilization but don't process
      while (this->available()) {
        uint8_t dummy;
        this->read_byte(&dummy);
      }
      return;
    } else {
      this->stabilizing_ = false;
      ESP_LOGD(TAG, "Stabilization complete, starting normal operation");
    }
  }

  // For Q&A mode, request periodically
  if (this->mode_ == MODE_QA) {
    if (now - this->last_request_ >= this->update_interval_) {
      ESP_LOGD(TAG, "Time to request new reading (interval: %u ms)", this->update_interval_);
      this->request_reading();
      this->last_request_ = now;
    }
  }

  // Count bytes received
  int bytes_read = 0;
  
  // Read data
  while (this->available() && bytes_read < 50) {  // Limit to prevent infinite loop
    uint8_t byte;
    this->read_byte(&byte);
    bytes_read++;
    
    // Log first few bytes for debugging
    if (bytes_read <= 5) {
      ESP_LOGV(TAG, "Byte %d: 0x%02X", bytes_read, byte);
    }

    if (this->mode_ == MODE_PASSIVE) {
      // Initiative mode (24 bytes)
      if (this->initiative_index_ == 0) {
        if (byte != HEADER_BYTE_1) {
          ESP_LOGV(TAG, "Passive: Looking for header 0x42, got 0x%02X", byte);
          continue;
        }
        ESP_LOGV(TAG, "Passive: Found header byte 1");
      } else if (this->initiative_index_ == 1) {
        if (byte != HEADER_BYTE_2) {
          ESP_LOGV(TAG, "Passive: Expected 0x4D, got 0x%02X, resetting", byte);
          this->initiative_index_ = 0;
          continue;
        }
        ESP_LOGV(TAG, "Passive: Found header byte 2");
      }
      
      this->initiative_buffer_[this->initiative_index_++] = byte;
      
      if (this->initiative_index_ == 24) {
        ESP_LOGD(TAG, "Received complete 24-byte frame in initiative mode");
        
        // Log the complete frame
        ESP_LOGV(TAG, "Frame: %02X %02X %02X %02X | %02X %02X %02X %02X | %02X %02X %02X %02X | %02X %02X %02X %02X | %02X %02X %02X %02X | %02X %02X %02X %02X",
                 this->initiative_buffer_[0], this->initiative_buffer_[1], this->initiative_buffer_[2], this->initiative_buffer_[3],
                 this->initiative_buffer_[4], this->initiative_buffer_[5], this->initiative_buffer_[6], this->initiative_buffer_[7],
                 this->initiative_buffer_[8], this->initiative_buffer_[9], this->initiative_buffer_[10], this->initiative_buffer_[11],
                 this->initiative_buffer_[12], this->initiative_buffer_[13], this->initiative_buffer_[14], this->initiative_buffer_[15],
                 this->initiative_buffer_[16], this->initiative_buffer_[17], this->initiative_buffer_[18], this->initiative_buffer_[19],
                 this->initiative_buffer_[20], this->initiative_buffer_[21], this->initiative_buffer_[22], this->initiative_buffer_[23]);
        
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
      if (this->qa_index_ == 0) {
        if (byte != QA_START_BYTE) {
          ESP_LOGV(TAG, "Q&A: Looking for 0xFF, got 0x%02X", byte);
          continue;
        }
        ESP_LOGV(TAG, "Q&A: Found start byte");
      } else if (this->qa_index_ == 1) {
        // Log what command byte we got
        ESP_LOGV(TAG, "Q&A: Command byte is 0x%02X", byte);
        if (byte != 0x86) {
          ESP_LOGV(TAG, "Q&A: Not a data response (0x86), skipping");
          this->qa_index_ = 0;
          continue;
        }
      }
      
      this->qa_buffer_[this->qa_index_++] = byte;
      
      if (this->qa_index_ == 9) {
        ESP_LOGD(TAG, "Received complete 9-byte Q&A response");
        ESP_LOGV(TAG, "Q&A Response: %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                 this->qa_buffer_[0], this->qa_buffer_[1], this->qa_buffer_[2], 
                 this->qa_buffer_[3], this->qa_buffer_[4], this->qa_buffer_[5],
                 this->qa_buffer_[6], this->qa_buffer_[7], this->qa_buffer_[8]);
        
        if (this->qa_buffer_[1] == 0x86) {
          if (this->validate_checksum_qa_(this->qa_buffer_)) {
            this->parse_qa_data_(this->qa_buffer_);
            this->last_transmission_ = now;
            this->waiting_for_response_ = false;
            ESP_LOGD(TAG, "Q&A data processed successfully");
          } else {
            ESP_LOGW(TAG, "Invalid checksum in Q&A mode");
          }
        } else {
          ESP_LOGV(TAG, "Ignoring non-data Q&A response");
        }
        this->qa_index_ = 0;
      }
    }
  }
  
  if (bytes_read > 0) {
    ESP_LOGV(TAG, "Read %d bytes total", bytes_read);
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
  // Extract PM values from correct positions (as per manual)
  uint16_t pm_1_0  = (data[10] << 8) | data[11];  // Bytes 11-12 in manual (0-indexed: 10-11)
  uint16_t pm_2_5  = (data[12] << 8) | data[13];  // Bytes 13-14 in manual (0-indexed: 12-13)
  uint16_t pm_10_0 = (data[14] << 8) | data[15];  // Bytes 15-16 in manual (0-indexed: 14-15)
  
  ESP_LOGD(TAG, "Initiative mode - PM1.0: %u, PM2.5: %u, PM10: %u µg/m³", pm_1_0, pm_2_5, pm_10_0);
  
  // Validate reasonable values (0-1000 µg/m³)
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
