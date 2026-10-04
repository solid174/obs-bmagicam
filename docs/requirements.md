# Requirements

obs-bmagicam is an open-source OBS Studio plugin. It turns an iPhone running the Blackmagic Camera app into a 60 fps camera on the local network, puts every camera setting of the app into an OBS dock where each change shows in the preview at once, offers one-click looks and stabilization, and keeps a computer microphone in sync with the iPhone's picture. Setup takes one click: pick the phone, pick a preset and a look, and the camera appears on the scene, set up for streaming. Version 1.1 adds a skin beautifier with a one-slider Beauty control and a Remote Control web panel ([Releases](#releases)).

Not affiliated with Blackmagic Design. "Blackmagic Camera" is used only to name the app the plugin works with.

## Components

| Component | OBS type | Purpose |
| --- | --- | --- |
| iPhone Camera | Source | Finds the phone, configures it and receives its video and audio |
| Camera Controls | Dock | Every camera setting of the phone, looks and resets, changed live |
| Add iPhone Camera | Tools menu action | Adds the source to the current scene with a stream preset and a look |
| Beautify (1.1) | Filter | Smooths skin on any video source, not only the iPhone Camera |
| Remote Control (1.1) | Tools menu dialog and local web server | Optional web panel and API for phones, tablets, Stream Deck and scripts |

All components ship in one plugin. Source and filter do not depend on each other.

## iPhone Camera

- **CAM-1** Finds phones on the local network that run Blackmagic Camera with its HTTP Server turned on, and lists them by device name. An IP address can be entered by hand instead.
- **CAM-2** Configures the phone's stream through the app's remote-control API. The only steps on the phone are one-time: install the app, allow Local Network access, turn on HTTP Server.
- **CAM-3** Presets set resolution, frame rate, bitrate, codec and orientation in one choice. The default preset gives the best picture the network allows, at 1080p60 landscape. Higher presets up to 4K are available; they add delay and need a strong connection, and say so. Portrait is available for vertical scenes. A preset also sets the phone's own recording format, because the phone streams in the format it records.
- **CAM-4** Receives video at the phone's full frame rate, 60 fps, together with the phone's audio.
- **CAM-5** Shows the connection state in the source properties: searching, connecting, live, or an error with a reason a non-technical user can act on.
- **CAM-6** Reconnects by itself after Wi-Fi drops or the app restarting, without user action.
- **CAM-7** Stops the phone's stream when the source is hidden, removed or OBS closes, so the phone does not keep streaming into nothing.
- **CAM-8** Several phones work at once, one source each.
- **CAM-9** Uses only the local network. No cloud, no accounts, no internet needed.
- **CAM-10** iPhone only. Blackmagic Camera for Android is not supported.

## Camera Controls

- **CTL-1** Shows every camera setting that Blackmagic Camera offers over its API: exposure, white balance, focus, lens and zoom, color, video format, the phone's monitoring tools, audio input and recording. A setting the connected phone does not support is not shown.
- **CTL-2** A change shows in the OBS preview while the slider is still moving, not only after it is released.
- **CTL-3** A change made on the phone itself shows in OBS within half a second.
- **CTL-4** A control that another setting locks, for example ISO while auto exposure is on, says what locks it and how to unlock it.
- **CTL-5** Clicking the picture in the source's Interact window sets the focus point, like tapping the phone screen.
- **CTL-6** One dock serves every phone in the scene collection; the user picks which phone it controls.
- **CTL-7** Settings that only change the phone's own screen or its recordings, not the picture OBS receives, are marked as such.
- **CTL-8** "Set up for streaming" prepares the camera the way a camera operator would for a live stream: a flicker-free shutter for the local mains frequency, exposure and white balance measured once and then held so they do not drift on stream, and continuous autofocus. Stabilization stays as the user set it, because only they know whether the phone is handheld. Every value stays adjustable afterwards.

## Looks

- **LOOK-1** Built-in looks are graded for live streams to a professional standard, from Natural to the rich, punchy picture of top Instagram and TikTok creators: Natural, Studio, Warm, Vivid, Soft and Cinematic.
- **LOOK-2** A look changes only color: saturation, contrast, tone and color balance. Exposure, focus and white balance stay as they are, so a look works in any lighting.
- **LOOK-3** Looks are applied by the phone before compression. They cost OBS nothing, and the phone screen shows the same picture.
- **LOOK-4** The current color can be saved as the user's own look. The user's looks can be renamed and deleted.
- **LOOK-5** In every built-in look skin stays natural and flattering, close to the skin-tone line on a vectorscope; colors are rich but never neon; highlights do not clip and shadows keep their detail.

## Stabilization

- **STB-1** The stabilization setting uses Blackmagic Camera's own video stabilization, which works from the phone's gyroscope before compression and steadies handheld and moving shots very well (tried on the test phone).
- **STB-2** The modes are the app's: Off, Standard, Cinematic and Extreme. The app's API switches only between Off and Standard and reports the other two as on (V-15), so the dock turns stabilization off and on (Standard) and shows on for any of the three; Cinematic and Extreme are chosen on the phone, and the dock says so. Each mode is explained by what it is for and what it costs: stabilization crops the picture, more in the stronger modes. Off suits a phone on a tripod.
- **STB-3** The plugin adds no stabilization of its own.

## Microphone sync

- **SYN-1** "Sync microphone" puts a microphone connected to the computer in step with the iPhone's picture, so a voice recorded on the computer stays lip-synced with the iPhone's video. The iPhone's own audio needs nothing: it arrives in sync (NFR-6). When the iPhone's sound and a computer microphone that is not synced both reach the mix, the dock warns that viewers hear the voice twice.
- **SYN-2** It measures the delay from sound that both microphones hear while the user talks or claps for a few seconds, without test tones, then sets that source's Sync Offset (the one in OBS's Advanced Audio Properties) and shows the value. Undo puts the previous offset back.
- **SYN-3** The result is within 10 ms. When it cannot measure, for example in silence or with the iPhone's microphone muted, it says so and changes nothing.

## Reset

- **RST-1** "Reset to camera defaults" returns every camera setting to how a fresh install of Blackmagic Camera has it.
- **RST-2** "Restore my settings" returns the phone to exactly how it was before obs-bmagicam first changed it, including its livestream destination.
- **RST-3** Every group of controls has its own reset to default.
- **RST-4** Both resets ask for confirmation first, and keep retrying until the phone confirms every value, so a dropped connection cannot leave the phone half-changed.

## Beautify (1.1)

- **BEA-1** Works on any video source.
- **BEA-2** One Beauty slider with a magic-wand icon, like TikTok's, sets everything at once. Advanced sliders: smoothing, texture, tone evening (blemishes and redness), sharpening of what is not skin, glow, mask softness. Each change shows in the preview immediately.
- **BEA-3** Styles: Natural, Soft, Glam, and the user's own. The Beauty slider sets how strongly the chosen style applies.
- **BEA-4** Smooths skin only. Eyes, brows, lips, hair and the background keep their detail and sharpness.
- **BEA-5** No visible artifacts: no seams at the mask edge, no plastic look, no flicker between frames.
- **BEA-6** A "show mask" switch displays what the filter treats as skin.
- **BEA-7** At zero strength the output is identical to the input.
- **BEA-8** Beautify retouches skin and nothing else. Face reshaping, makeup, whole-picture color (the looks do that on the phone), vignette and grain are not part of it.

## Add iPhone Camera

- **ONE-1** One action from the Tools menu: choose a phone, a stream preset and a look, and whether to set the camera up for streaming. The plugin adds the source to the current scene fitted to the canvas and starts the camera. From 1.1 it also offers a Beauty style and attaches Beautify with it.
- **ONE-2** Every setting it applies can be changed afterwards in the usual OBS properties.

## Setup

- **SET-1** The documentation and the Add iPhone Camera wizard show each step on the phone with screenshots: install Blackmagic Camera, turn on HTTP Server (two screens), allow Local Network access.
- **SET-2** The wizard's first page waits for a phone and moves on by itself as soon as one appears, so the user can follow the steps with OBS open.
- **SET-3** On the computer the only prompts are the operating system's own (macOS Local Network, Windows Firewall), and the guide says what to choose in each.

## Remote Control (1.1)

- **WEB-1** Off by default. When turned on, OBS serves a control panel and an API on the local network.
- **WEB-2** The panel works in the browser of a phone or tablet. It has the same controls as the Camera Controls dock, the looks and the Beautify sliders, and updates live.
- **WEB-3** The API offers everything the panel does, is documented and versioned, and suits Stream Deck, Bitfocus Companion and scripts.
- **WEB-4** Access needs a password, and only local network addresses are accepted.
- **WEB-5** Turning it on, the port, the password and the connect info with a QR code are in a Tools menu dialog, like OBS's WebSocket Server Settings.

## Look and feel

- **UI-1** Looks like part of OBS in every built-in theme (Default, Classic, Acri, Grey, Light, Rachni and System) and follows a theme change without a restart.
- **UI-2** Uses OBS's own building blocks: a dock, source properties, Tools menu entries and a wizard. No colors, fonts or window styles of its own: controls the plugin draws itself take every color and its font from the current theme.
- **UI-3** Scrolling through the dock never changes a setting. Sliders respond to the mouse wheel only when focused, as in OBS's own properties.
- **UI-4** Simple and Advanced modes. Simple, the default, is never crowded: seven rows on one page (Set up for streaming, look, brightness, warmth, lens, focus, stabilization; Beauty joins them in 1.1), plus a microphone row when OBS has a microphone of the computer's (SYN-1), in everyday words instead of camera terms, and a status line only when something needs attention. Advanced shows every control. The dock has both modes, and so will the web panel and the Beautify properties.
- **UI-5** Every control explains itself: a tooltip says in plain words what it does, what it changes in the picture and when to use it. In the web panel the same text opens with an ⓘ button, because touch screens have no hover.
- **UI-6** Controls are visual and direct, in the spirit of Blackmagic Camera: values large enough to read at a glance, changed by dragging rulers and clicking tiles and buttons rather than by typing, with short animations that show what changed. A text box appears only where something has to be typed.

## Hardware and power

Each stage runs only while it is in use, and the hardware it needs adds up only for what is turned on:

| In use | Adds | Minimum hardware for 1080p60 without dropped or lagged OBS frames |
| --- | --- | --- |
| iPhone Camera | Video decoding | Any PC that runs OBS. No dedicated GPU; hardware decoding is used when available |
| iPhone Camera at 4K | 4K HEVC decoding | Hardware HEVC decoding: most GPUs since 2016, Apple M1 class |
| Beautify (1.1) | Skin smoothing on the GPU | Integrated GPU, Intel Iris Xe or Apple M1 class |
| Beautify on selected faces (1.2) | Face detection and recognition | Dedicated GPU, GTX 1660 class, or Apple M1 class |

- **PWR-1** A stage that is off costs nothing: a hidden or disabled filter runs no GPU passes, and face detection loads no models until a mode needs it.
- **PWR-2** Turning a stage on or off takes effect immediately, without restarting OBS or the camera.

## Quality targets

- **NFR-1** Each combination in the table above meets its frame-rate target on the listed hardware.
- **NFR-2** Camera latency from phone to OBS preview stays under 400 ms at 1080p60 on 5 GHz Wi-Fi. 4K presets may add up to 200 ms.
- **NFR-3** Runs on Windows x64, macOS (Apple Silicon) and Linux x64, OBS Studio 32.2 or newer. Every release ships for all three.
- **NFR-4** All processing is local. Video never leaves the computer and the phone.
- **NFR-5** Settings UI and messages in English and Russian.
- **NFR-6** Sound and picture stay in sync: audio is never more than 40 ms ahead of or 60 ms behind the video (the broadcast lip-sync window, EBU R37), from the first frame, over streams lasting hours, and after every reconnect. Sync comes before latency.

## Licensing

- **LIC-1** The plugin is released under GPL-2.0-or-later.
- **LIC-2** All code is written for this project.
- **LIC-3** Third-party libraries and models keep their licenses and notices, shipped with each release.

## Releases

### 1.0

iPhone Camera, Camera Controls with looks, stabilization, resets and microphone sync, and Add iPhone Camera, as described above.

### 1.1: Beautify and Remote Control

Beautify (BEA-1 to BEA-8) and Remote Control (WEB-1 to WEB-5) as described above, the Beauty row in the dock, and the Beauty style in Add iPhone Camera.

### 1.2: Face detection for Beautify

The user chooses who Beautify applies to. Face detection runs only in Selected mode:

| Mode | Behavior | Face detection |
| --- | --- | --- |
| All | Everyone in the frame is beautified, as in 1.1 | Off |
| Selected | Only the chosen participants are beautified | On |
| Off | Nobody is beautified | Off |

- **FACE-1** Participants are chosen from the faces currently in the frame, by clicking them in a list of thumbnails.
- **FACE-2** A chosen participant stays chosen when they turn away, are briefly covered, or leave the frame and come back. Others entering the frame do not take their place.
- **FACE-3** The skin mask follows each face, including fast head turns, without slipping onto the background, hair or other people.
- **FACE-4** When a face is lost, the effect on it fades out smoothly and fades back in when the face is found again.
- **FACE-5** Up to 4 faces at 1080p60 on the hardware listed for selected faces.
- **FACE-6** Selections can be saved, so a participant is recognised again in the next session.
- **FACE-7** In Selected mode, where face landmarks exist, Beautify also lifts under-eye shadows and brightens eyes, keeps eyes, brows and lips out of the skin mask precisely, and scales smoothing to the size of each face.
