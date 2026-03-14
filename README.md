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

<br>
<br>

---
<br>

# HAL Implementation Details

## Front Panel LED Behavior (RPi4 ACT LED)

This implementation uses the Raspberry Pi's ACT LED as the Device Settings Status LED.

Front Panel LCD is not supported in this implementation.

### Brightness Behavior

- Front-panel brightness control is not supported in this implementation.

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

This table shows the front-panel LED timing behavior. The timing pattern is not a product specification.

Legend:
- ON segment: `█`
- OFF segment: `░`
- Each waveform uses a fixed width of 50 units (5 groups of 10) for side-by-side comparison.
- Bars are proportional for readability (exact timing remains in the timing column).

| State | Waveform (one cycle) | Exact Timing (ms) |
|---|---|---|
| `ACTIVE` | <nobr><code>██████████&#8288;██████████&#8288;██████████&#8288;██████████&#8288;██████████</code></nobr> | steady ON<br> |
| `STANDBY` | <nobr><code>░░░░░░░░░░&#8288;░░░░░░░░░░&#8288;░░░░░░░░░░&#8288;░░░░░░░░░░&#8288;░░░░░░░░░░</code></nobr> | steady OFF<br> |
| `WPS_CONNECTING` | <nobr><code>█████░░░░░&#8288;█████░░░░░&#8288;█████░░░░░&#8288;█████░░░░░&#8288;█████░░░░░</code></nobr> | 500 ON, 500 OFF<br> |
| `WPS_CONNECTED` | <nobr><code>█░█░░░░░░&#8288;█░█░░░░░░&#8288;█░█░░░░░░&#8288;█░█░░░░░░&#8288;█░█░░░░░░</code></nobr> | 120 ON, 120 OFF, 120 ON, 640 OFF<br> |
| `WPS_ERROR` | <nobr><code>█░█░█░█░█░&#8288;█░█░█░█░█░&#8288;█░█░█░█░█░&#8288;█░█░█░█░█░&#8288;█░█░█░█░█░</code></nobr> | 80 ON, 80 OFF<br> |
| `FACTORY_RESET` | <nobr><code>█████████░&#8288;░█████████&#8288;░░████████&#8288;█░░░░░░░░░&#8288;░░░░░░░░░░</code></nobr> | 900 ON, 250 OFF, 900 ON, 250 OFF, 900 ON, 1400 OFF<br> |
| `USB_UPGRADE` | <nobr><code>█░█░██████&#8288;█░░░░░░░░░&#8288;░░░░░█░█░█&#8288;██████░░░░&#8288;░░░░░░░░░░</code></nobr> | 120 ON, 120 OFF, 120 ON, 120 OFF, 700 ON, 1400 OFF<br> |
| `SOFTWARE_DOWNLOAD_ERROR` | <nobr><code>█░█░█░░░██&#8288;██░████░██&#8288;██░░░█░█░█&#8288;░░░░░░░░░░&#8288;░░░░░░░░░░</code></nobr> | 120/120, 120/120, 120/300, 400/120, 400/120, 400/300, 120/120, 120/120, 120/1400<br> |
