# Changelog

User-visible changes in each release. Versions follow [semantic versioning](https://semver.org).

## 1.0.1 (2026-10-04)

- When the phone says it streams but no picture arrives, the dock now first asks you to look at the phone's screen for a message from Blackmagic Camera, such as "Audio Source Unavailable", and only then to check the firewall and the network.

## 1.0.0 (2026-10-04)

The first release.

- **iPhone Camera source.** Finds iPhones running Blackmagic Camera 3.4 or newer on the network, or by address. Sets up the phone's livestream to OBS and receives it over SRT with hardware decoding: 1080p60 at about 400 ms glass to glass, presets up to 4K, the phone's audio in sync. Reconnects by itself after Wi-Fi drops, the app going to the background or the phone locking. Stops the phone's stream 2 s after the source is hidden, and gives the phone back its own livestream destination and video format when the source is removed or OBS closes. Click the picture in the source's Interact window to focus there.
- **Camera Controls dock.** Simple mode: Set up for streaming, looks, brightness, warmth, lens, focus, stabilization and the microphone. Advanced mode: a histogram, the camera's tiles with rulers for lens and zoom, shutter, iris, ISO, white balance and tint, the stream preset, color with the wheels, focus modes and distance, the phone's audio inputs, recording on the phone, dynamic range and recording codec, and the phone screen's tools. Changes show in the preview while you drag, changes made on the phone show in OBS, and every control explains itself in its tooltip. Follows the OBS theme.
- **Looks:** Natural, Studio, Warm, Vivid, Soft and Cinematic, applied by the phone before compression, and looks of your own.
- **Set up for streaming:** a flicker-free shutter, exposure and white balance measured once and held, continuous focus.
- **Microphone sync:** measures how far a computer microphone runs ahead of the iPhone's picture from your voice, sets its Sync Offset, and undoes it on request. The dock warns when viewers would hear you twice, from the iPhone and from an unsynced computer microphone.
- **Safe to try:** Reset to camera defaults, Restore my settings (the phone as it was before the plugin first changed it, also saved on the phone as a preset), and the phone's own presets.
- **Add iPhone Camera** wizard with the phone steps in pictures.
- English and Russian.
- Windows installer and `.zip`, macOS package, Ubuntu package.
