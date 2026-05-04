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
