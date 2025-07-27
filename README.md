# ESPHome ZH03B Particulate Matter Sensor

Custom ESPHome component for the ZH03B laser particulate matter sensor with support for both Passive and Q&A (Question & Answer) modes.

## Features

- ✅ PM1.0, PM2.5, and PM10.0 measurements
- ✅ UART communication with checksum validation
- ✅ **Dual Mode Support**: Passive and Q&A modes
- ✅ Power-efficient Q&A mode for battery applications
- ✅ Configurable update intervals
- ✅ Compatible with ESPHome
- ✅ Home Assistant integration ready
- ✅ Automatic data parsing and validation

## Supported Sensors

- ZH03B Laser Particulate Matter Sensor

## Operating Modes

### Passive Mode (Default)
- Sensor continuously sends data every second
- ESP32 passively receives and processes data
- Real-time measurements
- Higher power consumption

### Q&A Mode (Question & Answer)
- ESP32 sends request commands to sensor
- Sensor responds only when requested
- Configurable update intervals (30s, 60s, etc.)
- **Power efficient** - ideal for battery-powered projects
- Better for deep sleep applications

## Installation

### Method 1: External Components (Recommended)

Add to your ESPHome YAML configuration:

```yaml
external_components:
  - source: github://Bjk8kds/esphome-zh03b-sensor
    components: [ zh03b ]
```

### Method 2: Local Components

1. Download or clone this repository
2. Copy the `components/zh03b` folder to your ESPHome configuration directory
3. Add the component to your YAML configuration

## Wiring

| ZH03B Pin | ESP32 Pin | Description |
|-----------|-----------|-------------|
| VCC       | 5V        | Power supply |
| GND       | GND       | Ground |
| TX        | GPIO16    | Data transmission |
| RX        | GPIO17    | Data reception |

**Note:** Both TX and RX connections are required for Q&A mode to send commands to the sensor.

## Configuration

### Basic Configuration (Passive Mode)

```yaml
uart:
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 9600

sensor:
  - platform: zh03b
    mode: PASSIVE          # Default mode
    pm_1_0:
      name: "PM1.0"
    pm_2_5:
      name: "PM2.5"
    pm_10_0:
      name: "PM10.0"
```

### Power-Efficient Configuration (Q&A Mode)

```yaml
uart:
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 9600

sensor:
  - platform: zh03b
    mode: QA               # Question & Answer mode
    update_interval: 60s   # Request data every 60 seconds
    pm_1_0:
      name: "PM1.0"
    pm_2_5:
      name: "PM2.5"
    pm_10_0:
      name: "PM10.0"
```

## Configuration Options

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `mode` | string | `PASSIVE` | Operating mode: `PASSIVE` or `QA` |
| `update_interval` | time | `30s` | Interval for Q&A mode requests |
| `pm_1_0` | sensor | optional | PM1.0 sensor configuration |
| `pm_2_5` | sensor | optional | PM2.5 sensor configuration |
| `pm_10_0` | sensor | optional | PM10.0 sensor configuration |

## Mode Comparison

| Feature | Passive Mode | Q&A Mode |
|---------|--------------|-----------|
| Power Consumption | Higher | **Lower** |
| Real-time Data | ✅ Yes | Configurable intervals |
| Battery Friendly | ❌ No | ✅ **Yes** |
| Deep Sleep Compatible | ❌ Limited | ✅ **Yes** |
| Data Frequency | ~1Hz | User defined |
| UART TX Required | ❌ No | ✅ **Yes** |

## Example Use Cases

### Real-time Air Quality Monitor
```yaml
mode: PASSIVE  # For continuous monitoring
```

### Battery-Powered Outdoor Sensor
```yaml
mode: QA
update_interval: 5min  # Save power, update every 5 minutes
```

### IoT Sensor with Deep Sleep
```yaml
mode: QA
update_interval: 15min  # Wake up, measure, sleep
```

## Example Configuration

See [example/zh03b-example.yaml](example/zh03b-example.yaml) for a complete configuration example with both modes.

## Troubleshooting

### No data received
- Check wiring connections
- Verify UART pins configuration
- Ensure 5V power supply for the sensor
- For Q&A mode: Ensure both TX and RX are connected

### Invalid checksum errors
- Check for electromagnetic interference
- Verify stable power supply
- Try different UART pins
- Reduce update interval in Q&A mode

### Q&A Mode not working
- Verify TX pin is connected to sensor RX
- Check if sensor supports Q&A mode commands
- Increase timeout in logs
- Try switching back to PASSIVE mode

### Power consumption still high in Q&A mode
- Increase `update_interval` (try 2-5 minutes)
- Implement deep sleep between measurements
- Check other components power usage

## Debugging

Enable verbose logging to see detailed communication:

```yaml
logger:
  level: DEBUG
  logs:
    zh03b: DEBUG
```

## Contributing

Pull requests are welcome! Please read our contributing guidelines first.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Credits

- ESPHome community
- ZH03B sensor documentation
- Power optimization techniques from IoT community
