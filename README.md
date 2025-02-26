# rdkvhal-devicesettings-raspberrypi4
RDKV DeviceSettings HAL Implementation of RPi4

The implementation adheres to the DS HAL Interface (HALIF) v2.0.0 requirements and have been tested against the respective HAL Test Suites listed below.

#### For enabling debug logs from hal
Create `/etc/debug.ini` and add the following line
```
LOG.RDK.DSMGR = LOG DEBUG INFO ERROR
```
### Related Repositories

- **HAL Header Repository**: [rdk-halif-device_settings](https://github.com/rdkcentral/rdk-halif-device_settings) [v2.0.0](https://github.com/rdkcentral/rdk-halif-device_settings/releases/tag/2.0.0)
- **HAL Test Suite Repository**: [rdk-halif-test-device_settings](https://github.com/rdkcentral/rdk-halif-test-device_settings)
