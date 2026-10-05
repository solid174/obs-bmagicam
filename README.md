# obs-bmagicam

An OBS Studio plugin that turns an iPhone running the free Blackmagic Camera app into a 60 fps camera on your local network, with every camera setting in an OBS dock.

> **Status:** version 1.1 is out: [download it from Releases](https://github.com/solid174/obs-bmagicam/releases) and follow the [setup guide](docs/setup.md). It finds the phone, receives its 1080p60 stream at about 400 ms glass to glass with hardware decoding, reconnects by itself, and puts every camera setting, looks, Set up for streaming, resets and microphone sync in the Camera Controls dock. 1.1 adds Beautify and Remote Control; face tracking for Beautify is next ([milestones](docs/architecture.md#milestones)).

Not affiliated with Blackmagic Design. "Blackmagic Camera" is used only to name the app the plugin works with.

## What it does

- **iPhone as a network camera.** Finds the phone on your network, sets up its stream and receives 1080p60 video (up to 4K) with audio in sync. No cables, no capture card, no cloud.
- **Every camera setting in OBS.** Exposure, white balance, focus, lens and zoom, color, format, the phone's monitoring tools and audio, in a dock that looks like part of OBS. Changes show in the preview while you drag.
- **A professional picture in one click.** "Set up for streaming" prepares the camera the way a camera operator would, and looks from Natural to Vivid give the picture its style.
- **Stabilization** from the phone itself, Standard, Cinematic or Extreme, for handheld shots.
- **Microphone in sync.** A microphone on your computer runs ahead of the iPhone's picture; one click measures the delay from your voice and sets it.
- **Safe to try.** Reset the camera to its defaults, or restore exactly the settings your phone had before.
- **Beautify.** Skin smoothing for any video source, on the GPU: one Beauty slider, styles from Natural to Glam, and advanced values. Eyes, brows, lips, hair and the background stay sharp.
- **Remote Control.** A web panel for a phone or tablet with everything the dock has, and an API with live events for Stream Deck, Companion and scripts. Off until you turn it on; local network only, with a password.
- Windows, macOS and Linux. English and Russian.

## Requirements

- OBS Studio 32.2 or newer on Windows 10/11 (x64), macOS 12 or newer, or Ubuntu 24.04 (x64)
- An iPhone with Blackmagic Camera 3.4 or newer
- The iPhone and the computer on the same network

## Documentation

| Document | For |
| --- | --- |
| [Setup](docs/setup.md) | Installing the plugin, preparing the phone, first connection, troubleshooting |
| [Requirements](docs/requirements.md) | What each version must do |
| [User interface](docs/ui.md) | The dock, every control, properties, wizard, the web panel |
| [Architecture](docs/architecture.md) | How it is built, decisions, open questions, milestones |
| [Blackmagic Camera API](docs/camera-api.md) | The phone's API as verified on a real iPhone, with stream and latency measurements |
| [Remote Control API](docs/remote-api.md) | The plugin's own HTTP and WebSocket API |
| [Building and releasing](docs/releasing.md) | Builds for all three systems, CI, packaging, release test |
| [Changelog](CHANGELOG.md) | What changed in each release |

## License

[GPL-2.0-or-later](LICENSE)
