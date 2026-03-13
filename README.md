# rdkvhal-devicesettings-raspberrypi4
RDKV DeviceSettings HAL Implementation of RPi4

The implementation adheres to the DS HAL Interface (HALIF) v2.0.0 requirements and has been tested against the respective HAL test suites listed below.

#### To enable debug logs from HAL
Create `/etc/debug.ini` and add the following line
```
LOG.RDK.DSMGR = LOG DEBUG INFO ERROR
```
### Related Repositories

- **HAL Header Repository**: [rdk-halif-device_settings](https://github.com/rdkcentral/rdk-halif-device_settings) [v6.0.0](https://github.com/rdkcentral/rdk-halif-device_settings/releases/tag/6.0.0)
- **HAL Test Suite Repository**: [rdk-halif-test-device_settings](https://github.com/rdkcentral/rdk-halif-test-device_settings) [v6.0.0](https://github.com/rdkcentral/rdk-halif-test-device_settings/releases/tag/6.0.0)

## Front Panel LED Behavior (RPi4 ACT LED)

This implementation uses the Raspberry Pi's ACT LED as the Device Settings Status LED.

FrontPanel LCD is not supported in this implementation.

### Brightness Behavior

- Brightness APIs use Linux LED class sysfs (`brightness`, `max_brightness`).
- Platforms with binary LED behavior are treated as ON/OFF only.
- Intermediate brightness (for example 50%) returns `dsERR_OPERATION_NOT_SUPPORTED` on binary-only hardware.

### Supported LED States

- `ACTIVE`
- `STANDBY`
- `WPS_CONNECTING`
- `WPS_CONNECTED`
- `WPS_ERROR`
- `FACTORY_RESET`
- `USB_UPGRADE`
- `SOFTWARE_DOWNLOAD_ERROR`

### Timing Waveform Diagram (Rendered)

This table is the single source of truth for front-panel LED timing behavior.

Legend:
- ON segment: `█`
- OFF segment: `░`
- Each waveform uses a fixed width of 50 units (5 groups of 10) for side-by-side comparison.
- Bars are proportional for readability (exact timing remains in the timing column).

| State | Waveform (one cycle) | Exact Timing |
|---|---|---|
| `ACTIVE` | ██████████ ██████████ ██████████ ██████████ ██████████ | steady ON |
| `STANDBY` | ░░░░░░░░░░ ░░░░░░░░░░ ░░░░░░░░░░ ░░░░░░░░░░ ░░░░░░░░░░ | steady OFF |
| `WPS_CONNECTING` | █████░░░░░ █████░░░░░ █████░░░░░ █████░░░░░ █████░░░░░ | 500 ON, 500 OFF |
| `WPS_CONNECTED` | █░█░░░░░░ █░█░░░░░░ █░█░░░░░░ █░█░░░░░░ █░█░░░░░░ | 120 ON, 120 OFF, 120 ON, 640 OFF |
| `WPS_ERROR` | █░█░█░█░█░ █░█░█░█░█░ █░█░█░█░█░ █░█░█░█░█░ █░█░█░█░█░ | 80 ON, 80 OFF |
| `FACTORY_RESET` | █████████░ ░█████████ ░░████████ █░░░░░░░░░ ░░░░░░░░░░ | 900 ON, 250 OFF, 900 ON, 250 OFF, 900 ON, 1400 OFF |
| `USB_UPGRADE` | █░█░██████ █░░░░░░░░░ ░░░░░█░█░█ ██████░░░░ ░░░░░░░░░░ | 120 ON, 120 OFF, 120 ON, 120 OFF, 700 ON, 1400 OFF |
| `SOFTWARE_DOWNLOAD_ERROR` | █░█░█░░░██ ██░████░██ ██░░░█░█░█ ░░░░░░░░░░ ░░░░░░░░░░ | 120/120, 120/120, 120/300, 400/120, 400/120, 400/300, 120/120, 120/120, 120/1400 |
