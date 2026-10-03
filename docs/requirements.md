# Requirements

obs-bmagicam is an open-source OBS Studio plugin. It turns an iPhone running the Blackmagic Camera app into a 60 fps camera on the local network, and adds a skin beautifier filter with sliders. Setup takes one click: pick the phone, pick a preset, and the camera appears on the scene with the beautifier on.

Not affiliated with Blackmagic Design. "Blackmagic Camera" is used only to name the app the plugin works with.

## Components

| Component | OBS type | Purpose |
| --- | --- | --- |
| iPhone Camera | Source | Finds the phone, configures it and receives its video and audio |
| Beautify | Filter | Smooths skin on any video source, not only the iPhone Camera |
| Add iPhone Camera | Tools menu action | Adds the source to the current scene with Beautify and a preset |

All three ship in one plugin. Source and filter do not depend on each other.

## iPhone Camera

- **CAM-1** Finds phones on the local network that run Blackmagic Camera with its HTTP Server turned on, and lists them by device name. An IP address can be entered by hand instead.
- **CAM-2** Configures the phone's stream through the app's remote-control API. The only steps on the phone are one-time: install the app, allow Local Network access, turn on HTTP Server.
- **CAM-3** Presets set resolution, frame rate, bitrate, codec and orientation in one choice. The default preset gives the best picture the network allows, at 1080p60 landscape. Portrait is available for vertical scenes.
- **CAM-4** Receives video at the phone's full frame rate, 60 fps, together with the phone's audio.
- **CAM-5** Shows the connection state in the source properties: searching, connecting, live, or an error with a reason a non-technical user can act on.
- **CAM-6** Reconnects by itself after Wi-Fi drops or the app restarting, without user action.
- **CAM-7** Stops the phone's stream when the source is hidden, removed or OBS closes, so the phone does not keep streaming into nothing.
- **CAM-8** Several phones work at once, one source each.
- **CAM-9** Uses only the local network. No cloud, no accounts, no internet needed.

## Beautify

- **BEA-1** Works on any video source.
- **BEA-2** Sliders: smoothing, texture recovery, blemish removal, mask softness. Each change shows in the preview immediately.
- **BEA-3** Presets: Natural, Soft, Glam, and the user's own.
- **BEA-4** Smooths skin only. Eyes, brows, lips, hair and the background keep their detail and sharpness.
- **BEA-5** No visible artifacts: no seams at the mask edge, no plastic look, no flicker between frames.
- **BEA-6** A "show mask" switch displays what the filter treats as skin.
- **BEA-7** At zero strength the output is identical to the input.

## Add iPhone Camera

- **ONE-1** One action from the Tools menu: choose a phone and a preset. The plugin adds the source to the current scene fitted to the canvas, attaches Beautify with its default preset, and starts the camera.
- **ONE-2** Every setting it applies can be changed afterwards in the usual OBS properties.

## Hardware and power

Each stage runs only while it is in use, and the hardware it needs adds up only for what is turned on:

| In use | Adds | Minimum hardware for 1080p60 without dropped or lagged OBS frames |
| --- | --- | --- |
| iPhone Camera | Video decoding | Any PC that runs OBS. No dedicated GPU; hardware decoding is used when available |
| Beautify | Skin smoothing on the GPU | Integrated GPU, Intel Iris Xe or Apple M1 class |
| Beautify on selected faces (1.1) | Face detection and recognition | Dedicated GPU, GTX 1660 class, or Apple M1 class |

- **PWR-1** A stage that is off costs nothing: a hidden or disabled filter runs no GPU passes, and face detection loads no models until a mode needs it.
- **PWR-2** Turning a stage on or off takes effect immediately, without restarting OBS or the camera.

## Quality targets

- **NFR-1** Each combination in the table above meets its frame-rate target on the listed hardware.
- **NFR-2** Camera latency from phone to OBS preview stays under 300 ms on 5 GHz Wi-Fi.
- **NFR-3** Runs on Windows x64, macOS (Apple Silicon) and Linux x64, OBS Studio 31 or newer. Every release ships for all three.
- **NFR-4** All processing is local. Video never leaves the computer and the phone.
- **NFR-5** Settings UI and messages in English and Russian.

## Licensing

- **LIC-1** The plugin is released under GPL-2.0-or-later.
- **LIC-2** All code is written for this project.
- **LIC-3** Third-party libraries and models keep their licenses and notices, shipped with each release.

## Releases

### 1.0

iPhone Camera, Beautify and Add iPhone Camera as described above.

### 1.1: Face detection for Beautify

The user chooses who Beautify applies to. Face detection runs only in Selected mode:

| Mode | Behavior | Face detection |
| --- | --- | --- |
| All | Everyone in the frame is beautified, as in 1.0 | Off |
| Selected | Only the chosen participants are beautified | On |
| Off | Nobody is beautified | Off |

- **FACE-1** Participants are chosen from the faces currently in the frame, by clicking them in a list of thumbnails.
- **FACE-2** A chosen participant stays chosen when they turn away, are briefly covered, or leave the frame and come back. Others entering the frame do not take their place.
- **FACE-3** The skin mask follows each face, including fast head turns, without slipping onto the background, hair or other people.
- **FACE-4** When a face is lost, the effect on it fades out smoothly and fades back in when the face is found again.
- **FACE-5** Up to 4 faces at 1080p60 on the hardware listed for selected faces.
- **FACE-6** Selections can be saved, so a participant is recognised again in the next session.
