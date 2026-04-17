## Audio, Video and Display Management HAL Architecture

### Video and Display Management - Overview

This HAL uses a TVService broker model on Raspberry Pi 4 based on legacy Userland implementation.

- Daemon owns VCHI/TVService.
- HAL library client talk to the daemon over Unix socket IPC.
- Daemon forwards firmware callback events to subscribed clients.

#### Main Components

- Daemon: dsTVSvcDaemon.c
	- Owns TVService and gencmd lifecycle
	- Accepts client connections
	- Handles RPC requests and event fanout

- Client: dsTVSvcClient.c
	- Maintains one connection per process
	- Runs reader thread for responses/events
	- Provides synchronous RPC helper APIs

- HAL users (Only DSHAL Implementation)
	- dsDisplay.c
	- dsVideoPort.c
	- dsAudio.c

#### Lifecycle

Daemon startup:

1. vcos_init
2. vchi_initialise
3. vchi_connect
4. vc_vchi_tv_init
5. vc_vchi_gencmd_init
6. vc_tv_register_callback
7. start socket/event loop

Daemon shutdown:

1. vc_tv_unregister_callback
2. vc_gencmd_stop
3. vc_vchi_tv_stop
4. vchi_disconnect
5. close sockets and cleanup path

Client connect:

1. connect socket
2. start reader thread
3. subscribe for events
4. publish connection for RPC calls

#### IPC Model

- Protocol is defined in dsTVSvcProto.h.
- Messages use a fixed header: version, command, request id, payload length.
- RPC is request/response.
- Events are pushed asynchronously from daemon to client.

#### Concurrency Notes

Daemon:

- Main thread uses poll loop for client IO.
- Firmware callback thread enqueues events.
- Event queue is ring buffer based (head + count).

Client:

- One reader thread handles incoming traffic.
- RPC callers wait on condition variable.
- Reader control flags use atomic_bool.

#### Platform Behavior

- Current Raspberry Pi 4 implementation exposes HDMI path for display/video APIs.
- Unsupported display/port types return operation-not-supported.

#### Logging

- Daemon and client use HAL logger wrappers (`hal_info`, `hal_warn`, `hal_err`) for consistent runtime diagnostics.
- To enable debug logging, see [To enable debug logs from HAL](./README.md#to-enable-debug-logs-from-hal).

### Audio Management - Overview

Only supports bare minimal alsa based STEREO support on HDMI Audio Out Handle.
