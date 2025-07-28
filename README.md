# ESPHome ZH03B Particulate Matter Sensor

[![ESPHome](https://img.shields.io/badge/ESPHome-Compatible-blue.svg)](https://esphome.io/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

Custom ESPHome component for the ZH03B laser particulate matter sensor with support for both Initiative Upload (Passive) and Question & Answer (Q&A) modes.


> 🟢 **NB**: Example of using lambda directly in yaml (without this custom component), check `example`

## 🌟 Features

- 📊 **PM1.0, PM2.5, and PM10.0** measurements in µg/m³
- ✅ **Dual Mode Support**: Passive and Q&A modes
- 🔋 **Battery-friendly Q&A mode** with configurable intervals
- 🛡️ **Data validation** with checksum verification
- 🏠 **Home Assistant** integration ready
- 📡 **UART communication** at 9600 baud
- 🔧 **Customizable sensor parameters** (filters, device class, etc.)

## 📦 Installation

### Method 1: External Components (Recommended)

```yaml
external_components:
  - source: github://YourGitHubUsername/esphome-zh03b
    components: [ zh03b ]
```

### Method 2: Local Components

1. Clone this repository
2. Copy the `components/zh03b` folder to your ESPHome configuration directory
3. Add the local component reference to your YAML

## 🔌 Wiring

| ZH03B Pin | Function | ESP32 Pin | Wire Color |
|-----------|----------|-----------|------------|
| VCC | Power Supply | 5V | Red |
| GND | Ground | GND | Black |
| TX | Data Output | GPIO16 (RX) | Green |
| RX | Command Input | GPIO17 (TX) | Yellow |

> ⚠️ **Important**: Both TX and RX connections are required for Q&A mode operation

## ⚙️ Configuration

### Basic Setup (Passive Mode)

```yaml
# UART Configuration
uart:
  id: uart_zh03b
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 9600

# Sensor Configuration
sensor:
  - platform: zh03b
    uart_id: uart_zh03b
    pm_1_0:
      name: "PM 1.0"
    pm_2_5:
      name: "PM 2.5"
    pm_10_0:
      name: "PM 10.0"
```

### Power-Efficient Setup (Q&A Mode)

```yaml
# UART Configuration
uart:
  id: uart_zh03b
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 9600

# Sensor Configuration
sensor:
  - platform: zh03b
    uart_id: uart_zh03b
    mode: QA
    update_interval: 2min  # Request data every 2 minutes
    pm_1_0:
      name: "PM 1.0"
    pm_2_5:
      name: "PM 2.5"
    pm_10_0:
      name: "PM 10.0"
```

## 📊 Configuration Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `mode` | string | `PASSIVE` | Operating mode: `PASSIVE` or `QA` |
| `update_interval` | time | `60s` | Update interval for Q&A mode (min 30s) |
| `pm_1_0` | sensor | - | PM1.0 sensor configuration |
| `pm_2_5` | sensor | - | PM2.5 sensor configuration |
| `pm_10_0` | sensor | - | PM10.0 sensor configuration |
| `uart_id` | id | - | UART bus ID reference |

## 🔄 Operating Modes

### Passive Mode (Initiative Upload)
- Sensor continuously transmits data every second
- Real-time air quality monitoring
- No commands sent to sensor
- Higher power consumption

### Q&A Mode (Question & Answer)
- ESP32 controls when to request data
- Sensor sleeps between measurements
- Ideal for battery-powered applications
- Lower average power consumption
- Minimum 30s update interval, default 60s

## 💡 Use Cases

### Indoor Air Quality Monitor
```yaml
mode: PASSIVE  # Real-time monitoring
pm_2_5:
  name: "Living Room PM2.5"
  filters:
    - sliding_window_moving_average:
        window_size: 10
        send_every: 10
```

### Battery-Powered Weather Station
```yaml
mode: QA
update_interval: 5min
pm_2_5:
  name: "Outdoor PM2.5"
  filters:
    - median:
        window_size: 5
```

### Smart City Sensor Network
```yaml
mode: QA
update_interval: 15min
```

## 🔍 Troubleshooting

### No Data Received
- ✓ Check 5V power supply to sensor
- ✓ Verify UART pin connections
- ✓ Ensure correct baud rate (9600)

### Q&A Mode Issues
- ✓ Confirm TX pin is connected to sensor RX
- ✓ Verify update_interval ≥ 30s
- ✓ Enable debug logging to see commands

### Data Validation Errors
```yaml
logger:
  level: DEBUG
  logs:
    zh03b: VERY_VERBOSE
```

### Stuck Values in Passive Mode
- Sensor may need power cycle
- Try Q&A mode for more control
- Check for electromagnetic interference

## 🛠️ Development

### Project Structure
```
components/
└── zh03b/
    ├── __init__.py      # Empty file
    ├── sensor.py        # Python configuration
    ├── zh03b.h          # C++ header
    └── zh03b.cpp        # C++ implementation
```

### Debug Output
```
[D][zh03b:123]: Passive mode - PM1.0: 5, PM2.5: 12, PM10: 15 µg/m³
[D][zh03b:234]: Q&A: Sending READ_DATA command
[D][zh03b:345]: Q&A mode - PM1.0: 4, PM2.5: 11, PM10: 14 µg/m³
```

## 🤝 Contributing

Contributions are welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Commit your changes
4. Push to the branch
5. Open a Pull Request

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- ESPHome development team
- ZH03B sensor manufacturers
- Community contributors

## 📞 Support

- 📖 [ESPHome Documentation](https://esphome.io/)
- 💬 [Home Assistant Community](https://community.home-assistant.io/)
- 🐛 [Issue Tracker](https://github.com/Bjk8kds/esphome-zh03b-sensor/issues)

---

Made with ❤️ for the ESPHome community
