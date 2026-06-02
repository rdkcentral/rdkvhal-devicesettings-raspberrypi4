# AV Management Architecture

## Audio

### ALSA integration and interface

The HDMI audio path is implemented on ALSA using two control interfaces:

- ALSA control interface (`snd_ctl_*`) on `IEC958 Playback Default`.
- ALSA mixer/simple-element interface (`snd_mixer_*`) on either:
  - `SoftMaster` (preferred when present), or
  - `IEC958` (fallback).

Card selection uses:

- primary: `hw:0`
- fallback: `hw:1`

The HAL probes for IEC958 availability and selects the first usable HDMI card.

### Features exposed through ALSA

Implemented features are based on the available ALSA controls/elements:

- Audio encoding get/set:
  - Maps HAL encoding to IEC958 non-audio switch state.
  - Supports PCM passthrough off and compressed passthrough on.
- Mute/unmute:
  - Uses playback switch when available.
  - Falls back to volume-floor behavior for `SoftMaster` when needed.
- Audio level/gain/DB queries and updates:
  - Uses mixer playback volume range/value APIs where supported.
- Enable/disable behavior:
  - Built on top of mute semantics when direct control is unavailable.
- Stereo/compression reporting:
  - Derived from current encoding and cached HAL state when platform control is limited.

### Callback trigger model

Two audio-related callbacks are supported:

- Audio output connect callback (`dsAudioOutRegisterConnectCB`):
  - Registered in audio HAL.
  - Triggered by HDMI hotplug changes detected in display HAL watcher.
  - Display watcher sends connect/disconnect state through shared callback pointer.
- Audio format update callback (`dsAudioFormatUpdateRegisterCB`):
  - Triggered when `dsSetAudioEncoding()` changes effective audio format.

This design centralizes HDMI cable state detection in display/watcher logic while audio HAL remains callback registration owner.

## Video

### DRM/KMS integration

Video/display state is integrated through DRM helper functions used by HAL:

- HDMI connector state is queried through DRM-backed utility calls.
- Preferred mode detection is DRM-backed.
- A hotplug watcher thread uses udev DRM monitor events with periodic polling fallback.

The watcher tracks connection changes and publishes display events.

### Supported callbacks and behavior

Display callback path:

- Callback registration API: `dsRegisterDisplayEventCallback()`.
- Events emitted:
  - `dsDISPLAY_EVENT_CONNECTED`
  - `dsDISPLAY_EVENT_DISCONNECTED`

Behavior:

- Watcher snapshots callback pointer/state under mutex.
- Watcher releases mutex before invoking callback to avoid re-entry deadlocks.
- Audio hotplug notification is also issued from watcher on the same state transition.
- On each state transition, the watcher applies a settle delay and re-reads connector state before dispatching, suppressing transient connect/disconnect flapping during cable insertion. The delays are configurable at runtime:

  | Environment variable | Direction | Default |
  |---|---|---|
  | `DSHAL_HDMI_CONNECT_DEBOUNCE_MS` | connect | 25 ms |
  | `DSHAL_HDMI_DISCONNECT_DEBOUNCE_MS` | disconnect | 75 ms |

  Valid range is 0–2000 ms. Set to `0` to disable debounce for that direction.

Video format callback path (`dsVideoFormatUpdateRegisterCB`):

- `dsVideoPort` starts a dedicated video-format watcher thread during `dsVideoPortInit()`.
- Callback registration stores the callback and sets an initial-notify flag.
- The watcher waits ~10 ms for the initial notify path, then reports current format.
- Current format mapping on RPi4 is state-based:
  - disconnected or disabled HDMI -> `dsHDRSTANDARD_NONE`
  - connected and enabled HDMI -> `dsHDRSTANDARD_SDR`
- Subsequent notifications are emitted only when the watcher wakes and detects a change in one of these values:
  - connector connected state,
  - connector enabled state,
  - active mode token (resolution string).
- In the default event-driven path, connector connected/enabled transitions wake the watcher through the display connector-change hook.
- Active mode changes are observed immediately when they go through `dsSetResolution()`.
- Active mode changes made outside `dsSetResolution()` are not standalone wake events; they are only observed later if another wakeup occurs, or via the optional polling safety-net when enabled.

Cross-module event wiring:

- `dsDisplay` exposes a connector-change hook registration API (`dsRegisterConnectorChangeHook`).
- `dsVideoPort` registers `onHdmiConnectorChange` as this hook.
- On HDMI state transition, `dsDisplay` invokes this hook after display/audio callbacks, waking the video-format watcher immediately.

Polling safety-net control:

- Video-format watcher is event-driven by default (condition-variable waits only).
- Optional periodic fallback polling is enabled at runtime only when this file exists:
  - `/opt/.dshal-enable-polling-for-cbs`
- When enabled, watcher uses a 5-second timed wait as a safety-net in addition to event wakeups.

### Resolution change flow

Resolution set/get is handled in video-port HAL with DRM-aware validation around HDMI state:

1. Client calls `dsSetResolution()` with `dsVideoPortResolution_t`.
2. HAL validates handle and resolution fields.
3. Resolution string is parsed into width/height/progressive-or-interlaced/rate.
4. Missing width is inferred from known height mappings.
5. HAL applies mode using `westeros-gl-console set mode ...`.
6. HAL verifies apply result from command output and returns success/failure.
7. `dsGetResolution()` reports current mode through mapped resolution naming.

Connection status (`connected`/`enabled`) is retrieved via DRM connector-state helpers and used by display/video status APIs.
