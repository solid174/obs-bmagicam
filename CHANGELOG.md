# Changelog

User-visible changes in each release. Versions follow [semantic versioning](https://semver.org).

## 1.2.0 (unreleased)

- **Beautify follows faces:** it finds up to four faces in the picture and retouches only them, each as far as it is seen at any turn of the head, keeping eyes, brows and lips out precisely, so skin-colored clothes, wood or a wall are no longer smoothed, and nothing is when no face is in the picture. Smoothing scales with the size of the faces and the skin color is learned from them. A face fades in when found and out when lost, and is kept when it turns to profile. Runs on the CPU beside OBS, with Google's MediaPipe face models.

## 1.1.0 (2026-10-05)

- **Beautify:** a skin smoothing filter for any video source, on the GPU. Styles Natural, Soft and Glam and your own, one Beauty slider, and advanced values for smoothing, texture, tone evening, sharpening, glow, mask softness and detail size. The skin mask adapts to every skin tone and white balance and keeps eyes, brows, lips, hair and the background sharp; Show mask shows it. At 0 or turned off it leaves the picture untouched. In the dock as a Beauty row (Simple) and tab (Advanced), and as a choice in Add iPhone Camera.
- **Remote Control:** a web panel for a phone or tablet on the same network, with Simple and Advanced modes like the dock, live updates, Beautify for every source, and help on every control; and an API with live events for Stream Deck, Bitfocus Companion and scripts. Off by default; Tools → iPhone Camera Remote Control turns it on and shows a QR code that opens the panel signed in. Only the local network may connect, with a password.

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
