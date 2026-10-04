# Setup

From nothing to your iPhone live in OBS. You do the phone part once; after that, opening Blackmagic Camera is enough.

## What you need

- OBS Studio 32.2 or newer on Windows 10 or 11 (x64), macOS 12 or newer (Apple Silicon or Intel), or Ubuntu 24.04 (x64).
- An iPhone with **Blackmagic Camera 3.4 or newer** (free on the App Store). Remote control arrived in 3.4.
- The iPhone and the computer on the same network. 5 GHz Wi-Fi works best. Guest networks usually block devices from seeing each other. Connecting the computer to the iPhone's Personal Hotspot also works.

## 1. Install the plugin

Download the file for your system from the [releases page](https://github.com/solid174/obs-bmagicam/releases), close OBS, then:

| System | File | Install |
| --- | --- | --- |
| Windows | `obs-bmagicam-<version>-windows-x64.exe` | Run it. It finds OBS and installs into `C:\ProgramData\obs-studio\plugins\obs-bmagicam` |
| Windows, by hand | `obs-bmagicam-<version>-windows-x64.zip` | Unzip into `C:\ProgramData\obs-studio\plugins\` |
| macOS | `obs-bmagicam-<version>-macos-universal.pkg` | Open it and follow the installer |
| Ubuntu | `obs-bmagicam-<version>-x86_64-linux-gnu.deb` | `sudo apt install ./obs-bmagicam-<version>-x86_64-linux-gnu.deb` |

Start OBS. The Tools menu now has **Add iPhone Camera…**.

## 2. Prepare the iPhone (once)

1. Install **Blackmagic Camera** from the App Store and open it.
2. Turn on its HTTP server:

   1. Tap **Settings** at the bottom right.
   2. Under Network Access, tap **HTTP Server**.
   3. Turn on **Enable HTTP Server**. The switch turns blue.

   <p>
   <img src="../data/images/setup/iphone-1-settings.png" width="240" alt="Blackmagic Camera settings: 1, the Settings tab; 2, HTTP Server under Network Access">
   <img src="../data/images/setup/iphone-2-enable.png" width="240" alt="3: turn on Enable HTTP Server">
   <img src="../data/images/setup/iphone-3-enabled.png" width="240" alt="Enable HTTP Server turned on">
   </p>

3. When iOS asks whether Blackmagic Camera may find devices on your local network, tap **Allow**. If you tapped Don't Allow earlier: iPhone Settings → Privacy & Security → Local Network → Blackmagic Camera → on.
4. Keep the app open on screen while you stream, and set Auto-Lock to **Never** (iPhone Settings → Display & Brightness → Auto-Lock). iOS stops the app's server when the screen locks or you switch apps. A charger keeps a long stream going.

Under Settings → Remote Camera Control, **Camera Available for** must say **Control and Monitor**, which is the default.

## 3. Let OBS onto the network

The first time OBS talks to the phone, your computer may ask for permission:

| System | You see | Choose |
| --- | --- | --- |
| macOS | "OBS would like to find and connect to devices on your local network" | **Allow**. Changed your mind later: System Settings → Privacy & Security → Local Network → OBS |
| Windows | Windows Defender Firewall asks whether OBS may communicate on networks | **Allow** for private networks. Your Wi-Fi must be set to Private: Settings → Network & internet → Wi-Fi → your network → Private |
| Ubuntu | Nothing | If you run a firewall, open the stream ports: `sudo ufw allow 9710:9719/udp`. Phone discovery needs `avahi-daemon`, which Ubuntu runs by default |

## 4. Add the camera

1. In OBS: **Tools → Add iPhone Camera…**
2. The first page repeats the phone steps and waits for your phone. As soon as it appears, the wizard moves on.
3. Choose the stream preset (1080p60 High is right for most streams; the 4K presets look sharper but add delay and need strong 5 GHz Wi-Fi) and a look. Leave **Set up the camera for streaming** on. Hover over any choice to see what it does.
4. **Add Camera** adds it to the current scene, fitted to the canvas, and opens the Camera Controls dock with it. The first time it meets your phone, the plugin saves the phone's current settings, so you can always get them back. The picture appears within a few seconds.

## 5. Adjust the picture

- **View → Docks → Camera Controls** has every camera setting. Changes show in the preview while you drag.
- **Set up for streaming** (the button at the top of the dock) sets a flicker-free shutter, measures exposure and white balance once and holds them, and turns on continuous focus. Run it again if the light changes.
- **Look** gives the picture its style: Natural, Studio, Warm, Vivid, Soft or Cinematic. Adjust it in the Color tab, then **+** to save it as your own.
- **Focus on a spot:** right-click the source → **Interact**, then click where you want sharpness.
- **Advanced** (check box at the top of the dock) shows every camera setting: the histogram, the camera's ISO, shutter, white balance and lens, color, focus, the phone's audio and its own screen. Hover over any control to see what it does.
- **Phone presets** (⋮ menu) saves the camera's settings on the phone under a name and loads them again.

## 6. Sync a computer microphone

A microphone connected to the computer hears you about half a second before the iPhone's picture shows you. The iPhone's own audio is always in sync; if you use a computer microphone instead:

1. In the dock's **Microphone** row, choose the microphone.
2. Click **Sync** and talk or clap for about 12 seconds, with the iPhone's microphone on in Blackmagic Camera. It may be muted in OBS.
3. The button shows the delay it set, for example **Synced · 412 ms**, as the microphone's Sync Offset (Advanced Audio Properties). **Undo** puts the previous value back.

If it says it could not measure, nothing changed: talk or clap closer to both microphones and try again.

## 7. Reset

In the Camera Controls menu (⋮):

- **Reset to camera defaults** returns every camera setting to how a fresh install of Blackmagic Camera has it.
- **Restore my settings** returns the phone's settings to exactly how they were before obs-bmagicam first touched them. The same settings are also saved on the phone as the preset "Before obs-bmagicam". The phone's own livestream destination and video format come back whenever you remove the iPhone Camera or close OBS.

## Troubleshooting

| What you see | Why | What to do |
| --- | --- | --- |
| The wizard never finds the phone | The app is not on screen, HTTP Server is off, or the phone is on another network | Open Blackmagic Camera, check step 2, and make sure both devices use the same Wi-Fi, not a guest network |
| "Blackmagic Camera only allows monitoring" | Camera Available for is not set to Control and Monitor | In the app: Settings → Remote Camera Control → Camera Available for → Control and Monitor |
| Found, but "Blackmagic Camera isn't answering" | The phone locked or the app went to the background | Unlock the phone and bring the app back; OBS reconnects by itself |
| "The phone can't reach OBS" | The firewall blocks the stream, or the Wi-Fi is set to Public | Step 3 |
| Found on macOS only after a long wait, or never | OBS was denied local network access | System Settings → Privacy & Security → Local Network → OBS → on, then restart OBS |
| The picture stutters | Weak Wi-Fi or 2.4 GHz | Move closer to the router, use 5 GHz, or connect the computer to the iPhone's hotspot. A lower stream preset also helps |
| The phone gets hot | Long stream with the screen at full brightness | Dock → Phone → Phone screen → Brightness down, and keep the phone out of direct sun |
| "Update Blackmagic Camera" | App older than 3.4 | Update it from the App Store |

## Uninstall

- Windows: Settings → Apps → obs-bmagicam (OBS Studio plugin) → Uninstall, or delete `C:\ProgramData\obs-studio\plugins\obs-bmagicam`.
- macOS: delete `obs-bmagicam.plugin` from `~/Library/Application Support/obs-studio/plugins`.
- Ubuntu: `sudo apt remove obs-bmagicam`.

Before uninstalling, **Restore my settings** if you want your phone exactly as it was.
