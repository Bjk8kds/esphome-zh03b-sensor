#include "zh03b.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace zh03b {

static const char *const TAG = "zh03b";

// Q&A mode commands untuk ZH03B (9 bytes including checksum)
const uint8_t ZH03BSensor::CMD_READ_DATA[9] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
const uint8_t ZH03BSensor::CMD_SET_QA_MODE[9] = {0xFF, 0x01, 0x78, 0x41, 0x00, 0x00, 0x00, 0x00, 0x46};
const uint8_t ZH03BSensor::CMD_SET_PASSIVE_MODE[9] = {0xFF, 0x01, 0x78, 0x40, 0x00, 0x00, 0x00, 0x00, 0x47};

void ZH03BSensor::setup() {
  ESP_LOGCONFIG(TAG, "Setting up ZH03B...");
  this->data_index_ = 0;
  this->waiting_for_response_ = false;
  
  // Set mode sesuai konfigurasi
  if (this->mode_ == MODE_QA) {
    ESP_LOGCONFIG(TAG, "Setting ZH03B to Q&A mode");
    this->set_qa_mode();
  } else {
    ESP_LOGCONFIG(TAG, "Setting ZH03B to passive mode");
    this->set_passive_mode();
  }
}

void ZH03BSensor::loop() {
  const uint32_t now = millis();
  
  // Untuk mode Q&A, kirim request sesuai interval
  if (this->mode_ == MODE_QA) {
    if (now - this->last_request_ >= this->update_interval_) {
      this->request_reading();
      this->last_request_ = now;
    }
  }
  
  // Baca data yang masuk dari sensor
  while (this->available()) {
    uint8_t byte;
    this->read_byte(&byte);
    
    // Cek header bytes
    if (this->data_index_ == 0) {
      if (byte != ZH03B_HEADER_BYTE) {
        continue;
      }
    } else if (this->data_index_ == 1) {
      if (byte != ZH03B_SECOND_BYTE) {
        this->data_index_ = 0;
        continue;
      }
    }
    
    this->data_buffer_[this->data_index_] = byte;
    this->data_index_++;
    
    // Jika sudah menerima semua data
    if (this->data_index_ == ZH03B_RESPONSE_LENGTH) {
      this->parse_data_(this->data_buffer_);
      this->data_index_ = 0;
      this->waiting_for_response_ = false;
      this->last_transmission_ = now;
      break;
    }
  }
  
  // Timeout handling untuk Q&A mode
  if (this->mode_ == MODE_QA && this->waiting_for_response_) {
    if (now - this->last_request_ > 2000) { // 2 detik timeout
      ESP_LOGW(TAG, "Timeout waiting for sensor response");
      this->waiting_for_response_ = false;
      this->data_index_ = 0;
    }
  }
}

void ZH03BSensor::parse_data_(const uint8_t *data) {
  // Validasi checksum
  if (!this->validate_checksum_(data, ZH03B_RESPONSE_LENGTH)) {
    ESP_LOGW(TAG, "Invalid checksum received");
    return;
  }
  
  // Parse PM values (dalam ug/m3)
  uint16_t pm_1_0 = (data[6] << 8) | data[7];
  uint16_t pm_2_5 = (data[2] << 8) | data[3];
  uint16_t pm_10_0 = (data[4] << 8) | data[5];

  ESP_LOGD(TAG, "Received PM1.0: %u ug/m³, PM2.5: %u ug/m³, PM10: %u ug/m³", pm_1_0, pm_2_5, pm_10_0);
  
  // Publish ke sensor entities
  if (this->pm_1_0_sensor_ != nullptr) {
    this->pm_1_0_sensor_->publish_state(pm_1_0);
  }
  if (this->pm_2_5_sensor_ != nullptr) {
    this->pm_2_5_sensor_->publish_state(pm_2_5);
  }
  if (this->pm_10_0_sensor_ != nullptr) {
    this->pm_10_0_sensor_->publish_state(pm_10_0);
  }
}

bool ZH03BSensor::validate_checksum_(const uint8_t *data, uint8_t length) {
  if (length < 3) return false;
  
  uint16_t calculated_checksum = 0;
  for (uint8_t i = 0; i < length - 2; i++) {
    calculated_checksum += data[i];
  }
  
  uint16_t received_checksum = (data[length - 2] << 8) | data[length - 1];
  
  return calculated_checksum == received_checksum;
}

void ZH03BSensor::request_reading() {
  if (this->waiting_for_response_) {
    ESP_LOGD(TAG, "Still waiting for previous response, skipping request");
    return;
  }
  
  ESP_LOGD(TAG, "Requesting sensor reading");
  this->send_command_(CMD_READ_DATA, sizeof(CMD_READ_DATA));
  this->waiting_for_response_ = true;
  this->data_index_ = 0;
}

void ZH03BSensor::set_qa_mode() {
  ESP_LOGD(TAG, "Setting sensor to Q&A mode");
  this->send_command_(CMD_SET_QA_MODE, sizeof(CMD_SET_QA_MODE));
  this->mode_ = MODE_QA;
  delay(100); // Tunggu sensor mengubah mode
}

void ZH03BSensor::set_passive_mode() {
  ESP_LOGD(TAG, "Setting sensor to passive mode");
  this->send_command_(CMD_SET_PASSIVE_MODE, sizeof(CMD_SET_PASSIVE_MODE));
  this->mode_ = MODE_PASSIVE;
  delay(100); // Tunggu sensor mengubah mode
}

void ZH03BSensor::send_command_(const uint8_t *command, uint8_t length) {
  // Commands sudah termasuk checksum, kirim langsung
  this->write_array(command, length);
  ESP_LOGD(TAG, "Sent %d bytes command", length);
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
