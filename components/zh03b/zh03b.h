#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace zh03b {

enum ZH03BMode {
  MODE_PASSIVE = 0,  // Sensor mengirim data otomatis
  MODE_QA = 1        // Sensor hanya merespons ketika diminta (Q&A)
};

class ZH03BSensor : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void set_pm_2_5_sensor(sensor::Sensor *pm_2_5_sensor) { pm_2_5_sensor_ = pm_2_5_sensor; }
  void set_pm_1_0_sensor(sensor::Sensor *pm_1_0_sensor) { pm_1_0_sensor_ = pm_1_0_sensor; }
  void set_pm_10_0_sensor(sensor::Sensor *pm_10_0_sensor) { pm_10_0_sensor_ = pm_10_0_sensor; }
  void set_mode(ZH03BMode mode) { mode_ = mode; }
  void set_update_interval(uint32_t interval) { update_interval_ = interval; }
  
  // Getter methods untuk custom sensor
  sensor::Sensor *get_pm_1_0_sensor() { return pm_1_0_sensor_; }
  sensor::Sensor *get_pm_2_5_sensor() { return pm_2_5_sensor_; }
  sensor::Sensor *get_pm_10_0_sensor() { return pm_10_0_sensor_; }

  // Q&A mode methods
  void request_reading();
  void set_qa_mode();
  void set_passive_mode();

 protected:
  bool read_sensor_data_();
  bool validate_checksum_(const uint8_t *data, uint8_t length);
  void parse_data_(const uint8_t *data);
  void send_command_(const uint8_t *command, uint8_t length);

  sensor::Sensor *pm_2_5_sensor_{nullptr};
  sensor::Sensor *pm_1_0_sensor_{nullptr};
  sensor::Sensor *pm_10_0_sensor_{nullptr};

  ZH03BMode mode_{MODE_PASSIVE};
  uint32_t update_interval_{30000};  // 30 detik default
  uint32_t last_transmission_{0};
  uint32_t last_request_{0};
  uint8_t data_buffer_[9];
  uint8_t data_index_{0};
  bool waiting_for_response_{false};
  
  static const uint8_t ZH03B_RESPONSE_LENGTH = 9;
  static const uint8_t ZH03B_HEADER_BYTE = 0x42;
  static const uint8_t ZH03B_SECOND_BYTE = 0x4D;
  
  // Q&A mode commands
  static const uint8_t CMD_READ_DATA[9];
  static const uint8_t CMD_SET_QA_MODE[9];
  static const uint8_t CMD_SET_PASSIVE_MODE[9];
};