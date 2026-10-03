# Architecture

How obs-bmagicam is built to meet [requirements.md](requirements.md). The phone API it relies on is in [camera-api.md](camera-api.md), the user interface in [ui.md](ui.md), the plugin's own API in [remote-api.md](remote-api.md), and build and release in [releasing.md](releasing.md).

## Overview

```
iPhone · Blackmagic Camera                      OBS · obs-bmagicam
                                                
 Bonjour _http._tcp ─────────────────────────►  Discovery ───────────────┐
                                                                         ▼
 HTTPS :4444  REST   ◄── writes (coalesced) ──  CameraClient ──►  CameraState ──►  Camera Controls dock
 WSS   :4444  events ─── changes ────────────►       ▲                 │      └──►  Remote Control server ──► web panel, API
                                                     │                 │
                                                CameraSession ◄────────┘  (one per iPhone Camera source)
                                                     │
 SRT caller ── MPEG-TS (HEVC/H.264 + AAC) ─────►  StreamReceiver (FFmpeg) ──►  iPhone Camera source ──►  Beautify ──►  scene
```

- The phone is controlled through its REST API and reports every change over a WebSocket ([camera-api.md](camera-api.md)).
- Video and audio arrive as the phone's own livestream: the plugin adds a livestream destination that points at the PC, and the phone streams to it over SRT.
- One `CameraSession` per iPhone Camera source owns its phone connection, its stream and its receiver.
- `CameraState` holds what the phone can do and its current values. The dock, the web panel and the API all read it, and all write through the same `CameraClient`.

## OBS integration

| Component | OBS object | ID |
| --- | --- | --- |
| iPhone Camera | Source: async video, audio, interaction (`OBS_SOURCE_ASYNC_VIDEO`, `OBS_SOURCE_AUDIO`, `OBS_SOURCE_INTERACTION`, `OBS_SOURCE_DO_NOT_DUPLICATE`) | `bmagicam_camera` |
| Beautify | Video filter | `bmagicam_beautify` |
| Camera Controls | Dock, `obs_frontend_add_dock_by_id` | `bmagicam_controls` |
| Add iPhone Camera | Tools menu action opening a `QWizard`, `obs_frontend_add_tools_menu_qaction` | — |
| Remote Control | Tools menu action opening a dialog | — |

The iPhone Camera source outputs timestamped video frames and audio, so OBS keeps them in sync ([Audio and video sync](#audio-and-video-sync)). The Interact window's clicks become tap-to-focus (CTL-5): `mouse_click` maps the position to 0–1 and calls `PUT /lens/focus/doAutoFocus`.

## Source tree

```
src/
  plugin-main.cpp        module load and unload, registration, FFmpeg version check
  discovery/             DNS-SD browsing: dnssd-macos.cpp, dnssd-windows.cpp, dnssd-avahi.cpp
  phone/                 CameraClient: REST and WebSocket, write coalescing, retries
  camera/                CameraSession, CameraState, control descriptors, snapshots
  stream/                StreamReceiver: SRT listener, demux, decode, timestamps
  source/                iPhone Camera obs_source_info and properties
  looks/                 built-in and user looks, Set up for streaming
  filter/                Beautify obs_source_info
  remote/                HTTP server, WebSocket events, auth, static panel
  ui/                    Camera Controls dock, wizard, Remote Control dialog, widgets
data/
  locale/                en-US.ini, ru-RU.ini
  effects/               Beautify .effect files
  looks/builtin.json     built-in looks
  panel/                 web panel: index.html, app.js, app.css
  images/setup/          phone screenshots for the wizard
```

Modules depend downward only: `ui` and `remote` use `camera`; `camera` uses `phone`, `stream` and `discovery`. Nothing below `ui` uses Qt widgets, and nothing below `camera` knows about OBS sources.

## Camera session

One `CameraSession` per iPhone Camera source. It runs this state machine and reports each state in the source properties, the dock and the API (CAM-5):

| State | Meaning | What the user reads |
| --- | --- | --- |
| Searching | The phone's `device_id` is not on Bonjour and no manual address answers | "Looking for iPhone 17 Pro (A). Open Blackmagic Camera on the phone and keep it on screen." |
| Connecting | Found; opening REST and WebSocket | "Connecting to iPhone 17 Pro (A)…" |
| Starting | Destination selected, stream started, waiting for the first frame | "Starting the camera…" |
| Live | Frames arriving | "Live · 1080p60 · 12 Mb/s" |
| Paused | Source hidden, stream stopped (CAM-7) | "Paused while hidden" |
| Error | Something the user must fix | One sentence with the cause and the fix |

Examples of error texts, mapped from what the session observes:

| Observation | Text |
| --- | --- |
| Bonjour sees the phone but HTTPS does not answer | "Blackmagic Camera isn't answering. Bring the app back to the phone's screen." |
| `/access/status` is not `control-and-monitor` | "Blackmagic Camera only allows monitoring. Allow control in its HTTP Server settings." |
| Stream started but no SRT connection within 5 s | "The phone can't reach OBS. Allow OBS in the firewall, and keep the phone and computer on the same network, not a guest network." |
| `/livestreams/0/available` is false | The reason, translated: for example "The phone is playing back a clip." |
| App older than 3.4 (`version` in Bonjour TXT) | "Update Blackmagic Camera to version 3.4 or newer." |

Activation (CAM-7): the session streams while the source is shown anywhere (program, preview, projector, properties) and stops 2 s after it is hidden everywhere. The delay keeps scene switches between scenes that share the source from restarting the stream. The session also stops the stream on source removal and on OBS exit.

Reconnection (CAM-6): every failure leads back to Searching or Connecting with backoff (0.5, 1, 2, 4 s, then every 5 s). Auto-reconnect only restarts the stream; it does not re-apply camera settings.

### What the plugin owns and what the phone owns

| Settings | Owner | Applied |
| --- | --- | --- |
| Stream: video format, bitrate, destination | The iPhone Camera source (its stream preset) | On every connect and reconnect, because the stream follows the camera's video format |
| Color: the look | The iPhone Camera source (look values in the scene collection) | On the first connect in an OBS session, and whenever the user changes it. Color changes made on the phone update the stored values |
| Exposure, white balance, focus, lens, phone screen, audio | The phone | Only when the user changes them in the dock, the panel or Set up for streaming |

So a scene collection always reproduces its stream format and its look, while lighting-dependent settings are never overwritten behind the user's back.

## Control path

`CameraClient` speaks to one phone (facts in [camera-api.md](camera-api.md#transport)):

- **HTTPS client and WebSocket client** from cpp-httplib with Mbed TLS. Certificate verification is off: the app uses a self-signed certificate, and nothing secret travels on this connection (D8).
- **Requests are cheap but not free.** The phone closes every connection, so each request costs a TLS handshake: 35–110 ms on the test hotspot. Up to four requests run in parallel.
- **Write coalescing.** For each property at most one write is in flight. While it is in flight, newer values replace the pending one; when it completes, the latest pending value is sent. A dragged slider therefore produces a steady 10–25 writes per second, each with the latest value, and the preview follows the slider (CTL-2).
- **Echo handling.** While the user is changing a property, and for 300 ms after the last change, WebSocket events for that property do not move the control. Afterwards the control shows the value the phone confirmed.
- **Ordering.** Writes that depend on each other are sent in order: auto exposure off before ISO or shutter; video format before shutter; lens before zoom.
- **Errors.** 403 shows the lock reason (CTL-4). 404 or 501 at connect time hides the control (CTL-1). 400 reverts the control. Network errors retry with backoff, including during the pause after a format change.
- **Subscriptions.** On connect the client subscribes to every property in the control descriptors, plus livestream status, access status, power and storage. The subscribe response fills `CameraState` in one round trip. The WebSocket reconnects on its own.

### Control descriptors

One table in `camera/` describes every control: ID, group, phone endpoint and JSON field, value type (number, discrete list, enum, toggle, action), where its range comes from (a `description` or `supported…` endpoint), unit and display format, lock rules, whether it changes the stream (CTL-7), whether it appears in Simple mode (UI-4), the locale keys of its label and tooltip (UI-5), and its factory default. The dock builds its rows from this table, the API exposes it at `GET /api/v1/controls`, and the web panel renders from that. One list keeps the three surfaces identical. The full list is in [ui.md](ui.md#control-map).

## Video path

### Setup

When a session connects:

1. Pick the PC address the phone can reach: open a UDP socket towards the phone's address and read the local address the OS chose.
2. Pick the source's UDP port: the first free one from 9710 up, then keep it in the source settings.
3. Upload the destination `OBS on <PC name>` as Streaming XML, with one profile per stream preset ([recipe](camera-api.md#livestream-to-obs)).
4. Set the camera's video format from the stream preset (`PUT /system/videoFormat`), because the stream follows it.
5. Start the receiver listening, select the destination (`PUT /livestreams/0/activePlatform`) and start the stream (`PUT /livestreams/0/start`).
6. When the stream stops for good (source removed, OBS exit), select the destination that was active before, so the app's own livestreaming works as the user left it.

### Receiver

`StreamReceiver` (`stream/`) receives on FFmpeg, with two threads per connection:

- **Input.** `srt://0.0.0.0:<port>?mode=listener&transtype=live&latency=<µs>` through libavformat, with an interrupt callback for shutdown. No `avformat_find_stream_info`: decoders are created from the stream types in the program map. The first frame follows the first keyframe, which the phone sends every second.
- **Video.** HEVC or H.264 through libavcodec with hardware decoding (VideoToolbox on macOS, D3D11VA on Windows, VAAPI or CUDA on Linux; software, with slice threads only, when none works). Video waits as compressed packets, about 1.5 MB per second at 12 Mb/s, and is decoded 30 ms before its presentation time, so only a few decoded frames are ever held, however late the audio runs. Frames are copied to system memory (NV12, or P010 for 10-bit) and passed to `obs_source_output_video2` at their presentation time with their color space, range and transfer (SDR, HLG or PQ). At 1080p60 that copy is about 190 MB/s. A frame more than 50 ms overdue is skipped rather than shown late.
- **Audio.** AAC to float planar, 48 kHz stereo, passed to `obs_source_output_audio` as soon as it is decoded.
- **Timestamps.** One mapping for audio and video; see [Audio and video sync](#audio-and-video-sync).
- **Reconnect.** End of stream or an error closes the input and listens again immediately. The session meanwhile checks that the phone is still streaming and restarts it if not.
- **Check at load.** FFmpeg must have the majors the plugin was built against and receive SRT ([FFmpeg ABI](#ffmpeg-abi)); otherwise the source does not start its receiver.
- **Stats.** Frames per second, bitrate, late and dropped frames, decode errors, and the phone's reported send-buffer use. Shown in the Advanced status line, in the state tooltip and in the API; Simple mode only raises them when something is wrong.

### Audio and video sync

NFR-6 puts sync before latency. The phone stamps audio and video with one clock (the MPEG-TS presentation timestamps), and the receiver keeps that relation intact. OBS uses audio timestamps that lie within two seconds of its own clock as they are, so both streams go to OBS on OBS's clock:

- **One mapping** (`MediaClock`). A timestamp maps to itself plus the transport delay plus a 50 ms buffer, for audio and video alike. The transport delay is the greatest delay of any packet relative to its timestamp, over both streams, so the stream that arrives later, and the last packet of a burst, are still on time.
- **Settling.** Right after the phone connects, nothing is presented. The delay is measured once both streams are there and the burst of packets that follows the connection (200 ms) is over, for 300 ms, or for one second for a stream without audio. The first frame is then already on the final timeline.
- **Following.** Afterwards the delay follows the greatest delay per one-second window with a 30-second time constant, by at most 3 ms per window, which absorbs the drift between the phone's and the computer's clocks (tens of ppm). A packet that would arrive less than 10 ms before its presentation moves the map later at once; an excess of more than 100 ms that lasts three windows is dropped at once.
- **Gapless audio.** Audio goes to OBS without gaps. It follows the mapping by stretching or squeezing by at most 0.5 % (libswresample's compensation), so OBS never has to drop or insert audio; only a difference of more than 20 ms is closed with a jump.
- **Video on time.** Each frame goes to OBS at its presentation time, so OBS shows it in step with the audio.
- **Reconnect.** A new stream gets a new mapping before its first frame is shown, so a broken stream's timing never carries over.
- **Measured, not assumed.** A loopback test plays a 1080p60 HEVC + AAC stream with a white flash and a beep at the same timestamp every second into the receiver over SRT. Over 110 seconds, the beep reached OBS's timeline within 1.1 ms of its flash, with the sender's clock exact or off by ±300 ppm, no frame late after startup and no gap in the audio. V-16 still measures the phone with a clap test, one sharp event that is both seen and heard, in a recording at the start and after two hours.

### FFmpeg ABI

The plugin uses the FFmpeg that OBS ships: obs-deps on Windows and macOS, the distribution's FFmpeg on Linux (6.1, libavcodec 60, on Ubuntu 24.04). FFmpeg libraries change their binary interface with each major version, and OBS changed majors in the middle of the 32 series:

| OBS | obs-deps | FFmpeg | libavcodec |
| --- | --- | --- | --- |
| 31.0 – 32.1 | up to 2025-08-23 | 7.1 | 61 |
| 32.2 | 2026-07-15 | 8.1 | 62 |
| 33.0 (beta) | 2026-08-26 | 8.1 | 62 |

A build therefore supports the OBS versions that share its FFmpeg major. On Windows and Linux the library names carry the major version (`avcodec-62.dll`, `libavcodec.so.60`), so a build for another major does not load, and OBS names it in its "Plugin Load Error" message. OBS's macOS libraries are named without it (`@rpath/libavcodec.dylib`, compatibility version 62), so a build would load into an OBS with a newer major. At load the plugin therefore compares the runtime library versions with the ones it was built against. On a mismatch the iPhone Camera source does not start its receiver and shows an error naming the plugin version to install, instead of crashing OBS. The receiver code compiles against both FFmpeg 6.1 (Linux) and 8.1 (Windows, macOS).

### Latency budget

Measured on the test phone ([camera-api.md](camera-api.md#latency)):

| Stage | Time |
| --- | --- |
| Sound or light reaches the phone → packet arrives at the PC (1080p60, audio path; includes the 50–120 ms SRT buffer and the test's speaker delay) | about 350 ms |
| Buffer after the latest packet, which also covers decoding ([Audio and video sync](#audio-and-video-sync)) | 50 ms |
| Lag of the later stream in the phone's muxing, if any (V-18) | unknown |
| OBS render and display (one or two frames at 60 fps) | 17–33 ms |
| For comparison: the same through OBS's Media Source instead of the plugin's receiver | about 1240 ms |

That is about 420 ms end to end at 1080p60, a little over the NFR-2 target. The speaker delay inside the 350 ms is unknown, so V-1 measures video glass to glass, and V-2 and V-11 look for savings on the phone side. 4K adds about 180 ms (measured).

## Looks

A look is a set of color correction values: lift, gamma, gain and offset per channel, contrast and pivot, hue, saturation and luma contribution. Looks never touch exposure, white balance or focus (LOOK-2), and the phone applies them before compression (LOOK-3, verified in [camera-api.md](camera-api.md#color-correction)).

- Built-in looks live in `data/looks/builtin.json`. User looks live in the module config folder as `looks.json` (LOOK-4).
- Applying a look writes the seven `/colorCorrection/*` properties, each through the coalescer.
- Saving a look stores the current `CameraState` color values under a name.
- The source keeps its look values in the scene collection (see the [ownership table](#what-the-plugin-owns-and-what-the-phone-owns)).

Starting values for the built-in looks. They are tuned on the phone in development against a color chart and real skin tones, on a waveform and a vectorscope, until LOOK-5 holds:

| Look | Intent | Starting values |
| --- | --- | --- |
| Natural | Accurate, neutral | All neutral |
| Studio | Clean broadcast picture, a little richer than life | Contrast 1.08 at pivot 0.43, saturation 1.12 |
| Warm | Studio with warm, flattering skin | Studio, plus gain R 1.03, B 0.96 |
| Vivid | Rich, punchy creator look | Contrast 1.14 at pivot 0.43, saturation 1.28, gamma luma +0.02 |
| Soft | Gentle contrast for beauty and close-ups | Contrast 0.94, lift luma +0.015, saturation 1.08, gain luma 0.98 |
| Cinematic | Filmic, warm highlights over cool shadows | Contrast 1.10, saturation 0.92, lift B +0.01, gain R 1.02, B 0.98 |

The phone offers only global saturation and hue, not saturation by hue, so the looks rely on moderate saturation plus contrast and color balance, which keeps skin natural. Skin-aware color would need an OBS-side stage and is not part of 1.0.

### Set up for streaming (CTL-8)

1. Shutter: the value from `/video/flickerFreeShutters` closest to 1/(2 × frame rate). At 60 fps that is 1/100 under 50 Hz mains and 1/120 under 60 Hz. The phone's list already reflects the local mains frequency.
2. Exposure: auto exposure `OneShot` with the shutter fixed, wait until ISO settles, then `Off`, so brightness holds while streaming (V-13).
3. White balance: `PUT /video/whiteBalance/doAuto` once; it stays put afterwards.
4. Focus: continuous autofocus; face tracking where the phone offers it. Stabilization stays as the user set it (CTL-8).
5. Dynamic range `Video` (Rec.709), the range the looks are designed for. `Film` and `Extended Video` are for grading recordings, and `HLG` is HDR.
6. Optional: dim the phone's screen to 30 % to keep it cool during long streams.

Each step goes through the same coalescer, and every value stays adjustable in the dock afterwards.

## Reset and restore

- **Reset to camera defaults (RST-1):** `PUT /presets/active {"preset":"default"}`, then neutral color correction. If V-5 shows that `default` misses settings, the plugin applies a factory table for those settings, captured once from a fresh install of the app version it supports.
- **Before snapshot:** the first time the plugin connects to a phone (a `device_id` it has never seen), before writing anything, it does two things:
  - saves the phone's state as a phone preset `Before obs-bmagicam` (`PUT /presets/{name}`), which the user can also load in the app without OBS;
  - writes a JSON snapshot to `snapshots/<device_id>.json` in the module config folder: the active livestream destination, and every writable value from the control descriptors that V-6 shows a phone preset does not cover.
- **Restore my settings (RST-2):** load the phone preset, write the remaining snapshot values, select the original livestream destination and delete the plugin's destination.
- **Group reset (RST-3):** writes the descriptors' factory defaults for one group.
- **Reliability (RST-4):** a reset is a list of writes. The plugin retries each write until the phone confirms the value, and keeps an unfinished reset in the config folder so it resumes after a crash or a lost connection.

## Beautify

Beautify retouches skin and nothing else (BEA-8). It runs on the GPU as OBS `.effect` files, which OBS compiles for Direct3D 11, OpenGL and Metal.

The core idea comes from [ctbot000/face-beautifier](https://github.com/ctbot000/face-beautifier) (MIT), a WebGL beautifier: frequency separation, where small skin detail such as pores and blemishes is flattened and large detail such as lashes, nostrils and the lip line is kept, so heavy smoothing still does not look plastic. No code is taken from it (LIC-2); Beautify is written for OBS from scratch. What changes against the reference:

| Reference | Beautify |
| --- | --- |
| Gaussian base blur, which pulls color across face contours at high strength | Edge-aware base: a guided filter on luma, so contours stay clean |
| Fixed YCbCr thresholds for skin when there are no landmarks | Adaptive skin model: skin chroma is learned from the frame around the skin-tone line and follows it slowly, so it holds for every skin tone and white balance |
| Mask recomputed every frame | Mask blended over time, so nothing flickers (BEA-5) |
| Edge thresholds in absolute luma | Thresholds scaled by the measured image noise, so low and high ISO give the same result |
| Smoothing radius from the face width (landmarks) | 1.0: from the frame size and an advanced "Detail size". 1.1: from each face (FACE-7) |
| Reshaping, makeup, color grading, vignette, grain, compare, export | Dropped (BEA-8); color is the looks' job on the phone |
| Under-eye lift, eye brightening | Moved to 1.1, where face landmarks exist (FACE-7) |

Passes per frame:

1. Half-resolution copy, plus a slowly updated estimate of the image noise taken from flat areas.
2. Skin mask: adaptive chroma likelihood times a luminance gate, feathered by Mask softness, blended with the previous frame's mask.
3. Bases: a guided filter on luma at half resolution (fine), and a wider low-pass at quarter resolution (wide).
4. Composite at full resolution:
   - **Smoothing:** `detail = source − fine`. Inside the mask, small detail is attenuated by the strength, large detail (above a noise-scaled edge threshold) is kept. Texture keeps a share of the fine detail.
   - **Tone evening:** inside the mask, small dark spots are lifted towards the wide base and chroma is pulled towards the surrounding skin, which evens blemishes and redness.
   - **Sharpening:** outside the mask, `source − wide` is added back, so eyes, brows and hair look crisp next to smooth skin.
   - **Glow:** a soft bloom of the wide base on skin highlights.
5. Show mask draws the mask as an overlay (BEA-6).

**Style and strength (BEA-2, BEA-3).** A style is a full set of advanced values: Natural, Soft and Glam are built in, and the user's own come from the advanced sliders. The Beauty slider `s` from 0 to 1 sends each value through its own response curve, for example smoothing ∝ s^0.8, tone evening ∝ s, glow ∝ s², with texture kept higher at low `s`. The first half of the slider stays natural; the top reaches the full style. The curves are tuned on real faces in M3.

At `s = 0`, or when the filter is disabled, it calls `obs_source_skip_video_filter` and runs no passes (BEA-7, PWR-1). The work is about eight passes, most at half or quarter resolution; GPU time is measured with OBS's render statistics on the hardware in [requirements.md](requirements.md#hardware-and-power). SDR sources are processed as encoded, like a retouching tool does. HDR sources pass through untouched in 1.0.

Tests, after the reference's self-test idea: neutral settings are a bit-exact identity; smoothing acts only inside the mask and only on small detail. They run in CI on Linux with a software OpenGL context.

## Stabilization

The phone stabilizes from its gyroscope before encoding, and Blackmagic Camera offers four modes: Off, Standard, Cinematic and Extreme (STB-2). They steady handheld shots very well, so the plugin adds no stabilization of its own (STB-3). The setting maps onto the app's mode; the API offers `/lens/opticalImageStabilization` with `enabled` and `controlAvailable` (whether it can be changed in the current format), and V-15 establishes how the four modes are set through it, how much each crops and whether a mode adds delay to the livestream.

## Remote Control server

- cpp-httplib server on its own thread pool. Off by default (WEB-1); default port 4466, next to obs-websocket's 4455.
- **Access (WEB-4):**
  - accepts connections only from loopback and private addresses (10/8, 172.16/12, 192.168/16, 100.64/10, link-local, and IPv6 ULA and link-local), and refuses everything else with 403;
  - requires a password, which is generated on first use and changeable;
  - slows down repeated wrong passwords.
- Serves the web panel from `data/panel/`, the REST API under `/api/v1`, and live changes over a WebSocket at `/api/v1/events`. See [remote-api.md](remote-api.md).
- Handlers read `CameraState` snapshots and write through `CameraClient`, the same path as the dock. Beautify changes go through `obs_source_update` on the filter.
- The dialog in the Tools menu shows the address, password and a QR code made with qrcodegen, the generator OBS uses for its own WebSocket QR code.

## Persistence

| What | Where |
| --- | --- |
| Phone `device_id`, manual address, stream preset, look values, UDP port | iPhone Camera source settings (scene collection) |
| Beautify settings | Filter settings (scene collection) |
| Remote Control: on/off, port, password | `remote.json` in the module config folder |
| User looks, user Beauty styles | `looks.json`, `beauty-styles.json` |
| Before snapshots, unfinished resets | `snapshots/<device_id>.json` |
| Known phones (name, last address) | `devices.json` |
| Simple or Advanced mode | `ui.json` |

## Platform notes

| | macOS | Windows | Linux |
| --- | --- | --- | --- |
| Discovery | `dns_sd.h` (system) | `DnsServiceBrowse` (Windows 10 and later) | Avahi client |
| First prompt | Local Network access for OBS (V-12) | Windows Defender Firewall for OBS, inbound UDP | None; open UDP 9710–9719 if a firewall runs |
| Hardware decoding | VideoToolbox | D3D11VA | VAAPI |

## Dependencies

| Library | Used for | Windows, macOS | Linux | License |
| --- | --- | --- | --- | --- |
| libobs, obs-frontend-api | Plugin API | OBS SDK from the buildspec | `obs-studio` dev files | GPL-2.0-or-later |
| Qt 6 Widgets | Dock, wizard, dialogs | obs-deps | Distribution | LGPL-3.0 |
| FFmpeg (avformat, avcodec, avutil, swresample) | SRT input, demux, decode, audio drift compensation | obs-deps | Distribution | LGPL-2.1-or-later |
| Mbed TLS | TLS to the phone | obs-deps | Distribution | Apache-2.0 or GPL-2.0-or-later |
| nlohmann/json | JSON | obs-deps | Distribution | MIT |
| qrcodegen | Connect QR code | obs-deps | Distribution | MIT |
| cpp-httplib | HTTPS and WebSocket client, HTTP and WebSocket server | Fetched at a pinned release | Same | MIT |
| DNS-SD | Discovery | System | Avahi client | System, LGPL-2.1 |

Everything except cpp-httplib is already part of what OBS builds against. cpp-httplib is a single header.

## Decisions

| # | Decision | Why | Alternatives rejected |
| --- | --- | --- | --- |
| D1 | Video comes from the phone's livestream over SRT to a destination the plugin adds | The app has no other way to send video over the network; verified end to end at 1080p60 | HDMI or USB capture (needs hardware), NDI (not in the app), RTMP (needs an RTMP server; TCP stalls on Wi-Fi loss) |
| D2 | Own receiver on FFmpeg | OBS's Media Source added about 0.9 s and took 2.2 s to show the first frame | Wrapping `ffmpeg_source` |
| D3 | Link the dependencies OBS already ships; add only cpp-httplib | Least to build and maintain | Bundling an own FFmpeg (supports more OBS versions per build, at the cost of a full FFmpeg build per platform) |
| D4 | cpp-httplib for both the phone client and the plugin's server | One library for HTTPS, WSS, HTTP and WebSocket | Qt Network (OBS ships no Qt TLS backend or Qt WebSockets on macOS); libcurl (system builds lack WebSocket support) |
| D5 | Each OS's own DNS-SD service | Follows the OS's network and privacy rules | A portable raw-multicast library |
| D6 | Live controls in a dock, connection and stream in source properties | That is where OBS puts live controls and per-source settings (UI-2) | Everything in properties |
| D7 | Looks on the phone | Applied before compression, free for OBS, same on the phone screen (verified) | An OBS color filter |
| D8 | No certificate verification towards the phone | Self-signed certificate, no secrets on the connection | Trust on first use (more UI, no real gain on a LAN) |
| D9 | The phone owns lighting-dependent settings; the source owns stream and look | Reproducible look per scene collection without overwriting the user's exposure | Re-applying all stored camera values |

## Verification items

To settle in development, in the milestone named:

| # | Question | Milestone |
| --- | --- | --- |
| V-1 | Video latency glass to glass with the plugin's receiver (NFR-2) | M1 |
| V-2 | Does a low-latency profile exist in Streaming XML (`lowLatency` appears in the JSON profile schema), and how much does it save? | M1 |
| V-3 | Stream codec: H.264 when the camera records H.264? Does the profile's `codec` matter? | M1 |
| V-4 | How to get a portrait stream (vertical mode produced 1920×1080 landscape) | M1 |
| V-5 | What `PUT /presets/active {"preset":"default"}` resets | M2 |
| V-6 | What a saved phone preset contains (color, lens, phone screen, format) | M2 |
| V-7 | The other `/access/status` values. Known: the switch is Settings → Network Access → HTTP Server → Enable HTTP Server, and with Settings → Remote Camera Control → Camera Available for set to "Control and Monitor" the API reported `control-and-monitor` | M1 |
| V-8 | Several controllers at once; meaning of the TXT key `connected device` | M1 |
| V-9 | Units of `shutterAngle` on PUT | M2 |
| V-10 | Writing `normalized` instead of `normalised` on lens endpoints | M2 |
| V-11 | Does the phone hold SRT latency at 120 ms or more regardless of the PC's setting? | M1 |
| V-12 | macOS Local Network permission for OBS: OBS 32.2.2 has no `NSLocalNetworkUsageDescription`. Does the prompt appear, and do Bonjour and outgoing connections work from inside OBS? | M1 |
| V-13 | Which parameters auto exposure drives (`type`), and whether face-tracking autofocus exists on the back cameras | M2 |
| V-14 | What happens to a running stream when the app goes to the background | M1 |
| V-15 | Stabilization: how the API sets the app's modes Off, Standard, Cinematic and Extreme (`/lens/opticalImageStabilization` has only `enabled`), how much each crops, and whether a mode adds delay to the livestream | M1 |
| V-16 | Lip sync: offset between sound and picture in a recording, at the start and after two hours, against NFR-6 | M1 |
| V-17 | Does the app's Remote Password (Settings → Remote Camera Control) protect the HTTP API, and how is it sent? | M1 |
| V-18 | How far apart audio and video arrive relative to their timestamps in the phone's stream; the later one sets the delay | M1 |

## Decided after measurement

On 2026-10-03, after the tests in [camera-api.md](camera-api.md):

1. **Latency:** under 400 ms at 1080p60 (NFR-2). Sound and picture in sync is a hard requirement and comes before latency (NFR-6).
2. **OBS version:** 32.2 or newer (NFR-3). One build per platform, against FFmpeg 8 ([FFmpeg ABI](#ffmpeg-abi)).
3. **Android:** not supported (CAM-10).
4. **Recording format:** a stream preset also sets the phone's recording format, the 4K presets included (CAM-3).

## Milestones

Each milestone leaves a working plugin on all three platforms.

| Milestone | Delivers | Done when |
| --- | --- | --- |
| M0 Skeleton | Plugin from obs-plugintemplate; empty source, filter, dock and Tools entries; locale files; CI builds and packages for Windows, macOS and Linux | The packages install and load in OBS on all three |
| M1 Camera | Discovery, CameraClient, stream setup, receiver with hardware decoding, session states, reconnect, stop when hidden, minimal wizard | 1080p60 for 30 min without drops on all three; lip sync within NFR-6 at the start and after two hours; V-1 to V-4, V-7, V-8, V-11, V-12 and V-14 to V-18 answered |
| M2 Controls | Control descriptors with tooltips, the dock in Simple and Advanced modes, two-way sync, locks, tap-to-focus, Set up for streaming, looks, resets, phone stabilization | Every control in [ui.md](ui.md#control-map) works on the test phone; UI-4 and UI-5 hold; looks tuned until LOOK-5 holds |
| M3 Beautify | Beautify with styles, the Beauty slider, advanced sliders and show mask | BEA-1 to BEA-8 and NFR-1 met on the listed hardware |
| M4 Remote Control | Server, API, web panel, Tools dialog | WEB-1 to WEB-5; the panel controls everything the dock does |
| M5 Release | Full wizard with screenshots, Russian, theme pass, signing and notarization, documentation | The release checklist in [releasing.md](releasing.md) passes |
