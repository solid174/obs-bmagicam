# obs-bmagicam

An OBS Studio plugin that turns an iPhone running the free Blackmagic Camera app into a 60 fps camera on your local network, with every camera setting in an OBS dock.

> **Status:** in development ([milestones](docs/architecture.md#milestones)). M0, the skeleton, is done. M1, the camera, is under way: the receiver and the phone setup work against a real iPhone; finding phones on the network and the setup wizard come next. The documentation below describes version 1.0, and there is no release yet.

Not affiliated with Blackmagic Design. "Blackmagic Camera" is used only to name the app the plugin works with.

## What it does

- **iPhone as a network camera.** Finds the phone on your network, sets up its stream and receives 1080p60 video (up to 4K) with audio in sync. No cables, no capture card, no cloud.
- **Every camera setting in OBS.** Exposure, white balance, focus, lens and zoom, color, format, the phone's monitoring tools and audio, in a dock that looks like part of OBS. Changes show in the preview while you drag.
- **A professional picture in one click.** "Set up for streaming" prepares the camera the way a camera operator would, and looks from Natural to Vivid give the picture its style.
- **Stabilization** from the phone itself, Standard, Cinematic or Extreme, for handheld shots.
- **Beautify**, a skin smoothing filter for any video source.
- **Safe to try.** Reset the camera to its defaults, or restore exactly the settings your phone had before.
- **Remote Control** (optional): a web panel for a phone or tablet, and an API for Stream Deck, Companion and scripts.
- Windows, macOS and Linux. English and Russian.

## Requirements

- OBS Studio 32.2 or newer on Windows 10/11 (x64), macOS 12 or newer, or Ubuntu 24.04 (x64)
- An iPhone with Blackmagic Camera 3.4 or newer
- The iPhone and the computer on the same network

## Documentation

| Document | For |
| --- | --- |
| [Setup](docs/setup.md) | Installing the plugin, preparing the phone, first connection, troubleshooting |
| [Requirements](docs/requirements.md) | What 1.0 must do |
| [User interface](docs/ui.md) | The dock, every control, properties, wizard, web panel |
| [Architecture](docs/architecture.md) | How it is built, decisions, open questions, milestones |
| [Blackmagic Camera API](docs/camera-api.md) | The phone's API as verified on a real iPhone, with stream and latency measurements |
| [Remote Control API](docs/remote-api.md) | The plugin's own HTTP and WebSocket API |
| [Building and releasing](docs/releasing.md) | Builds for all three systems, CI, packaging, release test |

## License

[GPL-2.0-or-later](LICENSE)
