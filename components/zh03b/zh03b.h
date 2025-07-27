#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace zh03b {

enum ZH03BMode {
  MODE_PASSIVE = 0,  // Initiative upload mode (default)
  MODE_QA = 1        // Question & Answer mode
};

class ZH03BSensor : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void set_pm_1_0_sensor(sensor::Sensor *pm_1_0_sensor) { pm_1_0_sensor_ = pm_1_0_sensor; }
  void set_pm_2_5_sensor(sensor::Sensor *pm_2_5_sensor) { pm_2_5_sensor_ = pm_2_5_sensor; }
  void set_pm_10_0_sensor(sensor::Sensor *pm_10_0_sensor) { pm_10_0_sensor_ = pm_10_0_sensor; }
  void set_mode(ZH03BMode mode) { mode_ = mode; }
  void set_update_interval(uint32_t interval) { update_interval_ = interval; }
  
  void request_reading();
  void set_qa_mode();
  void set_passive_mode();

 protected:
  // Buffer untuk initiative mode (24 bytes)
  uint8_t initiative_buffer_[24];
  uint8_t initiative_index_{0};
  // Buffer untuk Q&A mode (9 bytes)
  uint8_t qa_buffer_[9];
  uint8_t qa_index_{0};

  // Sensor pointer
  sensor::Sensor *pm_1_0_sensor_{nullptr};
  sensor::Sensor *pm_2_5_sensor_{nullptr};
  sensor::Sensor *pm_10_0_sensor_{nullptr};

  ZH03BMode mode_{MODE_PASSIVE};
  uint32_t update_interval_{60000};  // 60 seconds default
  uint32_t last_transmission_{0};
  uint32_t last_request_{0};
  bool waiting_for_response_{false};
  
  // Stabilization tracking
  bool stabilizing_{false};
  uint32_t stabilization_start_{0};

  // Header
  static const uint8_t HEADER_BYTE_1 = 0x42;
  static const uint8_t HEADER_BYTE_2 = 0x4D;
  static const uint8_t QA_START_BYTE = 0xFF;

  // Commands (9 bytes)
  static const uint8_t CMD_READ_DATA[9];
  static const uint8_t CMD_SET_QA_MODE[9];
  static const uint8_t CMD_SET_PASSIVE_MODE[9];

  // Fungsi internal
  bool validate_checksum_initiative_(const uint8_t *data);
  bool validate_checksum_qa_(const uint8_t *data);
  void parse_initiative_data_(const uint8_t *data);
  void parse_qa_data_(const uint8_t *data);
  void send_command_(const uint8_t *command, uint8_t length);
};

}  // namespace zh03b
}  // namespace esphome
