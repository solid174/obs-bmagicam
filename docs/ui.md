# User interface

How obs-bmagicam looks and behaves inside OBS. Requirements are in [requirements.md](requirements.md); the phone endpoints behind each control are in [camera-api.md](camera-api.md).

## Principles

1. **Part of OBS.** Only standard Qt widgets, styled by the current OBS theme. No colors, fonts or style sheets of our own (UI-1, UI-2).
2. **Live.** A slider changes the camera while it moves, and the OBS preview shows the result (CTL-2). Changes made on the phone show up in OBS within half a second (CTL-3).
3. **Honest.** A control appears only if the connected phone supports it (CTL-1). A locked control says why (CTL-4). Settings that only affect the phone's own screen or recordings are marked (CTL-7).
4. **The same everywhere.** The dock, the web panel and the API are built from one list of controls, the [control map](#control-map).
5. **Simple first.** Simple mode puts the essentials on one page in everyday words; Advanced shows everything (UI-4). A streamer who never opens Advanced still gets a professional picture, and never sees a word like ISO or Kelvin.
6. **Self-explaining.** Every control has a tooltip in plain words: what it does, what it changes in the picture, when to use it (UI-5). See [Tooltips](#tooltips).

## Where things are

| Place | What | Why there |
| --- | --- | --- |
| View → Docks → **Camera Controls** | Live camera controls, looks, Set up for streaming, resets, status | Visible next to the preview while adjusting |
| iPhone Camera **Properties** | Phone, stream preset, look, status | Per-source settings live in properties in OBS (ONE-2) |
| Source context menu → **Interact** | Click to focus | OBS's own way to click into a source (CTL-5) |
| Tools → **Add iPhone Camera…** | Setup wizard | ONE-1, SET-1, SET-2 |
| Tools → **iPhone Camera Remote Control…** | Remote Control settings and connect info | WEB-5; mirrors Tools → WebSocket Server Settings |
| Filters → **Beautify** | Style and the Beauty slider; advanced sliders and show mask behind "Advanced settings" | Any video source (BEA-1) |
| A browser on a phone or tablet | Web panel | WEB-2 |

## Theme rules

- Widgets: `QLabel`, `QSlider`, `QSpinBox`, `QDoubleSpinBox`, `QComboBox`, `QCheckBox`, `QPushButton`, `QToolButton`, `QGroupBox`, `QTabWidget`, `QScrollArea`, laid out with `QFormLayout`. OBS's themes style all of them.
- Never call `setStyleSheet`, never set a color, font or palette.
- Text roles and icons come from OBS's theme classes, set with `widget->setProperty("class", "…")`. All of these exist in OBS 32.2.2's `Yami.obt` and `System.obt`, except `text-muted`, which only Yami-based themes define; in System such text simply shows as normal text.

  | Class | Used for |
  | --- | --- |
  | `text-muted` | Units, hints, the status line |
  | `text-success`, `text-warning`, `text-danger` | Connection state and warnings |
  | `icon-revert` | Group reset buttons (RST-3) |
  | `icon-dots-vert` | Dock menu button |
  | `icon-gear` | Opening Remote Control settings |
  | `icon-refresh` | Search for phones again |
  | `icon-plus`, `icon-trash` | Save and delete a user look |

- Icons come from the theme so they always match it. One exception: no OBS theme has a magic wand, so Beauty's icon ships as a single monochrome SVG, recolored at runtime with the palette's button text color. The only other images are the phone screenshots in the wizard.
- Sizes come from the theme and Qt. The dock has a minimum width of 280 px and no fixed heights. Russian text is about a third longer than English; labels wrap rather than cut off.
- On `OBS_FRONTEND_EVENT_THEME_CHANGED` the dock re-polishes widgets whose state classes changed. Everything else follows the theme automatically.
- Sliders and spin boxes ignore the mouse wheel unless they have focus, so scrolling through the dock never changes a setting (UI-3), the same as OBS's own properties.

## Camera Controls dock

Two modes (UI-4). The Advanced check box in the header switches between them, and the choice is remembered.

Without an iPhone Camera source in the scene collection, the dock shows one line instead: "Add an iPhone with Tools → Add iPhone Camera..., then adjust its camera here."

### Simple mode

The default. Everything a streamer needs, on one page, in everyday words:

```
┌ Camera Controls ─────────────────────────────────────────────────┐
│ [iPhone 17 Pro (A) · Main Camera   ▾]  ● Live  ☐ Advanced [⋮]    │
│                                                                  │
│                  [ Set up for streaming ]                        │
│ Look           [Studio                              ▾]           │
│ ✦ Beauty       [Natural ▾]  ━━━━━━━━●━━━━━━━━━━━━━━   55         │
│ Brightness     ━━━━━━●━━━━━━━━━━━━━━━━━━━━━━━━   ☐ Auto          │
│ Warmth         ━━━━━━━●━━━━━━━━━━━━━━━━━━━━━━━   [Auto]          │
│ Lens           [Front] [0.5×] [1×] [2×] [4×] [8×]                │
│ Focus          ☑ Auto   [Refocus]                                │
│ Stabilization  [Off                                 ▾]           │
└──────────────────────────────────────────────────────────────────┘
```

| Row | Controls | Behavior |
| --- | --- | --- |
| Set up for streaming | `camera.setUpForStreaming` | CTL-8 |
| Look | `look` | Built-in and user looks |
| Beauty | `beauty.style`, `beauty.strength` | The Beautify filter on this source: style and one strength slider, with the magic-wand icon (BEA-2). Without a Beautify filter the row shows "Add Beauty" |
| Brightness | `exposure.iso`; Auto is `exposure.mode` Continuous | Steps through the phone's ISO values without naming them. With Auto on, the slider follows the camera and is disabled |
| Warmth | `wb.temperature`, `wb.auto` | Cool to warm, no Kelvin numbers |
| Lens | `lens.camera` | Front, then one button per back lens, labelled the way the iPhone camera labels them (0.5×, 1×, 2×…) |
| Focus | `focus.mode` (Continuous or One shot), `focus.refocus` | The tooltip mentions clicking the picture in Interact |
| Stabilization | `lens.stabilization` | The app's modes: Off, Standard, Cinematic, Extreme (STB-2) |

Zoom, the color sliders and everything else stay in Advanced. A status line appears under the rows only when something needs attention, for example "Phone battery at 15 %. Connect a charger." The stream details (format, bitrate, frame rate) are in Advanced and in the state tooltip.

### Advanced mode

Every control in the [control map](#control-map), in tabs:

```
┌ Camera Controls ─────────────────────────────────────────────────┐
│ [iPhone 17 Pro (A) · Main Camera   ▾]  ● Live  ☑ Advanced [⋮]    │
│ Look  [Studio                           ▾]   [+]  [-]            │
│ ┌ Image │ Color │ Lens │ Beauty │ Phone ─────────────────────┐   │
│ │ ┌ Exposure ─────────────────────────────────────── [↺] ┐   │   │
│ │ │ Auto exposure   [Off                          ▾]     │   │   │
│ │ │ ISO             ━━━━━━●━━━━━━━━━━━━━━━   ISO 400     │   │   │
│ │ │ Shutter         ━━━━●━━━━━━━━━━━━━━━━━   1/100 ✓     │   │   │
│ │ └──────────────────────────────────────────────────────┘   │   │
│ │ ┌ White balance ────────────────────────────────── [↺] ┐   │   │
│ │ │ Temperature     ━━━━━━━●━━━━━━━━━━━━━━    4500 K     │   │   │
│ │ │ Tint            ━━━━━━━━━━●━━━━━━━━━━━       +15     │   │   │
│ │ │                                        [Auto WB]     │   │   │
│ │ └──────────────────────────────────────────────────────┘   │   │
│ │                                     [Set up for streaming] │   │
│ └────────────────────────────────────────────────────────────┘   │
│ 1080p60 · HEVC · 12.1 Mb/s · 60 fps · Battery 50 %               │
└──────────────────────────────────────────────────────────────────┘
```

### Header, menu and status line

These are the same in both modes:

- **Phone picker:** lists every iPhone Camera source in the scene collection by phone and source name (CTL-6). It follows the selection in the Sources list when an iPhone Camera source is selected.
- **State:** the session state with its theme color class; the full text is in the tooltip and in the source properties (CAM-5).
- **Menu (⋮):**
  - Set up for streaming (CTL-8)
  - Phone presets: load, save current as…, delete
  - Reset to camera defaults… (RST-1)
  - Restore my settings… (RST-2)
  - Setup guide
  - Remote Control…

  Both resets ask for confirmation and show progress until the phone confirms every value (RST-4).
- **Look:** built-in looks, then the user's looks. After a manual color change the entry reads "Studio (changed)"; in Advanced, [+] saves it as a new look (LOOK-4).
- **Tabs (Advanced):** Image, Color, Lens, Beauty, Phone. Each group has a reset button (↺, `icon-revert`) that restores that group's defaults (RST-3).
- **Status line:** in Advanced, format, codec, bitrate, frame rate and phone battery, from the receiver stats and the phone. In Simple, only warnings. Warnings such as low battery or a filling phone send buffer use `text-warning`.

### Control behavior

| Kind | Widget | Behavior |
| --- | --- | --- |
| Continuous value (temperature, tint, focus, zoom, color) | Slider plus spin box, like OBS's slider properties | Sends while dragging, coalesced ([architecture.md](architecture.md#control-path)) |
| Discrete list (ISO, shutter) | Slider over the phone's supported values, with a value label | Snaps to the supported values. Flicker-free shutter values carry a ✓ and a tooltip |
| Choice | Combo box | Applies on selection |
| Switch | Check box | Applies on toggle |
| Action | Button | Disabled while the action runs |
| Read-only | Label, `text-muted` | Updates from the phone |

- A control another setting locks stays visible and disabled, shows the live value, and its tooltip says how to unlock it, for example "Controlled by auto exposure. Set Auto exposure to Off to adjust ISO." (CTL-4)
- A control the phone does not support is not shown (CTL-1).
- The Phone tab's "Phone screen" group carries the note "Shown on the phone only, not in OBS", and the codec and proxy rows say "Recordings only" (CTL-7).

## Tooltips

Every control explains itself (UI-5):

- In the dock, the tooltip sits on the row's label and on the control. OBS's themes style tooltips.
- In properties, it is the property's long description (`obs_property_set_long_description`), which OBS shows as its own ⓘ hint next to the property.
- In the web panel, an ⓘ button opens the same text under the control, since touch screens cannot hover.

Each tooltip is two short sentences: what the control does, then what it changes in the picture or when to use it. Advanced controls may add a third line with the exact value and unit. The texts live in the locale files next to the labels, keyed by control ID (`Control.wb.temperature.Tooltip`), so they are translated with everything else. The control descriptors carry the key, and the API returns the text.

Examples:

| Control | Tooltip |
| --- | --- |
| Set up for streaming | Sets the camera up the way a camera operator would: no flicker from room lights, steady brightness and color, continuous focus. Run it again when your lighting changes. |
| Look | The color style of the picture, applied in the phone before it is sent. Natural is true to life; Vivid is rich and punchy. |
| Beauty | Smooths skin while eyes, hair and background stay sharp. Slide right for more; 0 is off. |
| Brightness | Makes the picture brighter or darker. Turn Auto off to keep brightness steady while you stream. |
| Warmth | Shifts colors cooler (blue) or warmer (orange) to match your lights. Auto sets it once from what the camera sees. |
| Lens | Switches between the phone's cameras. Front faces you like a selfie; the back lenses give the best picture. |
| Stabilization | Steadies handheld shots: Standard for small shakes, Cinematic for smooth moves, Extreme for walking. It crops the picture, more in the stronger modes, so leave it Off on a tripod. |
| ISO (Advanced) | How sensitive the sensor is. Higher is brighter but grainier; keep it as low as your light allows. |
| Shutter (Advanced) | How long each frame is exposed. Values marked ✓ avoid flicker from room lights; at 60 fps, 1/120 (1/100 where mains power is 50 Hz) looks most natural. |
| Phone screen brightness (Advanced) | Brightness of the phone's own screen; the stream does not change. Lower keeps the phone cooler on long streams. |

## Control map

Every setting the app offers over its API, with the ID used by the dock, the web panel and the [remote API](remote-api.md). Each row also has a tooltip ([Tooltips](#tooltips)). "Stream" says whether OBS sees the change. Values are from the test phone (iPhone 17 Pro, front ultra-wide camera); the plugin always reads ranges and lists from the phone.

### Image

| Group | Control | ID | Widget | Phone endpoint | Test phone | Stream |
| --- | --- | --- | --- | --- | --- | --- |
| Exposure | Auto exposure | `exposure.mode` | Choice: Off, Continuous, One shot | `/video/autoExposure` | Continuous | yes |
| Exposure | ISO | `exposure.iso` | Discrete | `/video/iso`, `/video/supportedISOs` | 20–1920 | yes, locked under auto exposure |
| Exposure | Shutter | `exposure.shutter` | Discrete, flicker-free marked | `/video/shutter`, `/video/supportedShutters`, `/video/flickerFreeShutters` | 1/60–1/8000 | yes, locked under auto exposure |
| Exposure | Shutter display | `exposure.shutterDisplay` | Choice: speed, angle | `/video/shutter/measurement` | speed | — |
| Exposure | Iris | `exposure.iris` | Continuous | `/lens/iris` | hidden: fixed aperture | yes |
| White balance | Temperature | `wb.temperature` | Continuous, K | `/video/whiteBalance` | 2500–10000 | yes |
| White balance | Tint | `wb.tint` | Continuous | `/video/whiteBalanceTint` | −50…50 | yes |
| White balance | Auto white balance | `wb.auto` | Action | `/video/whiteBalance/doAuto` | — | yes |
| — | Set up for streaming | `camera.setUpForStreaming` | Action | several ([architecture.md](architecture.md#set-up-for-streaming-ctl-8)) | — | yes |

### Color

| Group | Control | ID | Widget | Phone endpoint | Range | Stream |
| --- | --- | --- | --- | --- | --- | --- |
| Basic | Saturation | `color.saturation` | Continuous | `/colorCorrection/color` | 0–2 | yes |
| Basic | Contrast | `color.contrast` | Continuous | `/colorCorrection/contrast` | 0–2 | yes |
| Basic | Contrast pivot | `color.pivot` | Continuous | `/colorCorrection/contrast` | 0–1 | yes |
| Basic | Hue | `color.hue` | Continuous | `/colorCorrection/color` | −1…1 | yes |
| Basic | Luma mix | `color.lumaMix` | Continuous | `/colorCorrection/lumaContribution` | 0–1 | yes |
| Wheels (behind "Show wheels") | Lift R, G, B, luma | `color.lift.r` … `color.lift.y` | Continuous | `/colorCorrection/lift` | −2…2 | yes |
| Wheels | Gamma R, G, B, luma | `color.gamma.*` | Continuous | `/colorCorrection/gamma` | −4…4 | yes |
| Wheels | Gain R, G, B, luma | `color.gain.*` | Continuous | `/colorCorrection/gain` | 0–16 | yes |
| Wheels | Offset R, G, B, luma | `color.offset.*` | Continuous | `/colorCorrection/offset` | −8…8 | yes |

### Lens

| Group | Control | ID | Widget | Phone endpoint | Test phone | Stream |
| --- | --- | --- | --- | --- | --- | --- |
| Lens | Camera | `lens.camera` | Choice, e.g. "Back 1× · 24 mm" | `/lens/cameras`, `/lens/cameras/active` | 7 lenses | yes |
| Lens | Automatic lens switching | `lens.auto` | Switch | `/lens/cameras/auto` | hidden: not supported | yes |
| Lens | Zoom | `lens.zoom` | Continuous, shows mm | `/lens/zoom` | 19–570 mm | yes |
| Lens | Stabilization | `lens.stabilization` | Choice: Off, Standard, Cinematic, Extreme (STB-2) | `/lens/opticalImageStabilization` (V-15) | on | yes |
| Focus | Autofocus | `focus.mode` | Choice: Off, One shot, Continuous, Track face, Track object (as supported) | `/lens/focus/autoFocus` | One shot, Continuous | yes |
| Focus | Focus | `focus.position` | Continuous, near to far | `/lens/focus` | 0–1 | yes, locked under continuous autofocus |
| Focus | Refocus | `focus.refocus` | Action | `/lens/focus/autoFocus/retrigger` | — | yes |
| Focus | Focus point | `focus.point` | Action: "Center"; clicking in Interact sets any point | `/lens/focus/autoFocus/target`, `/lens/focus/doAutoFocus` | 0.5, 0.5 | yes |
| Focus | Focus state | `focus.state` | Read-only: Focusing, Focused, Too close, Lost tracking | `/lens/focus/autoFocus` | Idle | — |

### Beauty

These drive the Beautify filter on the selected source, so they work in OBS, not on the phone.

| Control | ID | Widget | Range | Mode |
| --- | --- | --- | --- | --- |
| Beauty on | `beauty.enabled` | Switch | — | Simple (with Add Beauty when the source has no filter) |
| Style | `beauty.style` | Choice: Natural, Soft, Glam, the user's styles | — | Simple |
| Beauty | `beauty.strength` | Continuous, magic-wand icon | 0–100; 0 is off | Simple |
| Smoothing | `beauty.smoothing` | Continuous | 0–100 | Advanced |
| Texture | `beauty.texture` | Continuous | 0–100 | Advanced |
| Tone evening | `beauty.evening` | Continuous | 0–100 | Advanced |
| Sharpening | `beauty.sharpen` | Continuous | 0–100 | Advanced |
| Glow | `beauty.glow` | Continuous | 0–100 | Advanced |
| Mask softness | `beauty.maskSoftness` | Continuous | 0–100 | Advanced |
| Detail size | `beauty.detailSize` | Continuous | 0–100 | Advanced |
| Show mask | `beauty.showMask` | Switch | — | Advanced |
| Save as style | `beauty.saveStyle` | Action | — | Advanced |

### Phone

| Group | Control | ID | Widget | Phone endpoint | Test phone | Stream |
| --- | --- | --- | --- | --- | --- | --- |
| Format | Resolution and frame rate | `format.video` | Choice | `/system/videoFormat`, `/system/supportedVideoFormats` | 720p–4032×3024, 23.98–60 | yes: the stream follows it |
| Format | Dynamic range | `format.dynamicRange` | Choice | `/system/dynamicRange` | Video, Extended Video, Film, HLG | yes |
| Format | Recording codec | `format.codec` | Choice | `/system/format`, `/system/supportedCodecFormats` | ProRes, HEVC, H.264 | recordings only (V-3) |
| Format | Off-speed recording | `format.offSpeed` | Switch and frame rate | `/system/format` | 4–60 fps | recordings only |
| Recording | Record on phone | `record.active` | Action: start, stop | `/transports/0/record`, `/transports/0/stop` | — | — |
| Recording | Proxy recording | `record.proxy` | Switch | `/transports/0/proxyRecording` | on | recordings only |
| Recording | Timecode, storage left | `record.timecode`, `record.storage` | Read-only | `/transports/0/timecode`, `/media/workingset` | 4 h 43 min left | — |
| Recording | Slate: scene, take, good take, auto-increment | `slate.*` | Text, number, switch | `/slates/nextClip`, `/slates/takeAutoIncrement` | — | recordings only |
| Audio | Input, per channel | `audio.N.input` | Choice | `/audio/channel/N/input`, `…/supportedInputs` | iPhone Microphone | yes |
| Audio | Level, per channel | `audio.N.level` | Continuous, dB | `/audio/channel/N/level` | hidden: built-in mic fixed | yes |
| Audio | Low cut, pad, phantom power | `audio.N.lowCut`, `.pad`, `.phantom` | Switch | `/audio/channel/N/…` | hidden: not supported | yes |
| Phone screen | Brightness | `screen.brightness` | Continuous, % | `/monitoring/Device/brightness` | 80 | phone only |
| Phone screen | Zebra and level | `screen.zebra`, `screen.zebraLevel` | Switch, choice 75–100 % | `/monitoring/Device/zebra`, `/monitoring/zebra` | off, 85 % | phone only |
| Phone screen | Skin-tone zebra | `screen.skinZebra` | Choice | `/monitoring/zebra` | off | phone only |
| Phone screen | Focus assist, mode, color, intensity | `screen.focusAssist*` | Switch, choices, continuous | `/monitoring/Device/focusAssist`, `/monitoring/focusAssist` | off | phone only |
| Phone screen | False color | `screen.falseColor` | Switch | `/monitoring/Device/falseColor` | off | phone only |
| Phone screen | Frame guide and ratio | `screen.frameGuide`, `screen.frameGuideRatio` | Switch, choice | `/monitoring/Device/frameGuide`, `/monitoring/frameGuideRatio` | off, 2:1 | phone only |
| Phone screen | Grids | `screen.grids` | Check boxes: Thirds, Crosshair, Dot, Horizon (two at most, one of them Thirds) | `/monitoring/frameGrids`, `/monitoring/Device/frameGrids` | none | phone only |
| Phone screen | Safe area and size | `screen.safeArea`, `screen.safeAreaPercent` | Switch, % | `/monitoring/Device/safeArea`, `/monitoring/safeAreaPercent` | off, 85 % | phone only |
| Phone screen | Display LUT | `screen.displayLut` | Switch | `/monitoring/Device/displayLUT` | on | phone only |
| Phone | Camera number | `camera.id` | Number 0–255 | `/camera/id` | 0 | — |
| Phone | Battery, app version | `phone.battery`, `phone.version` | Read-only | `/camera/power`, `/system/product` | 50 %, 3.5 | — |

Not shown because the iPhone does not support them: gain, ND filter, detail sharpening, color bars, tally, program feed, timing reference, clean feed, pre-record.

## iPhone Camera properties

| Property | Type | Notes |
| --- | --- | --- |
| Status | Info text; info, warning or error style | The session state text (CAM-5) |
| Phone | List | Phones found on the network by name, plus "Enter address…" (CAM-1) |
| Address | Text | Only with "Enter address…" |
| Stream preset | List | See below; its description says it also sets the phone's recording format |
| Look | List | Built-in and user looks |
| Open Camera Controls | Button | Shows the dock and selects this source in it |
| Setup guide | Button | Opens the wizard's first page |
| Advanced settings | Switch | Off by default; shows the three rows below (UI-4) |
| UDP port | Number | Chosen automatically, from 9710 up |
| SRT latency | Number, ms | Default 120 |
| Hardware decoding | Switch | Default on |

Stream presets (CAM-3). A preset also sets the phone's recording format. The bitrates are starting points; 12 Mb/s at 1080p60 measured clean on the test hotspot:

| Preset | Camera video format | Bitrate | Description shown to the user |
| --- | --- | --- | --- |
| 1080p60 High (default) | 1920×1080, 60 fps | 12 Mb/s | Best for most streams |
| 1080p60 Balanced | 1920×1080, 60 fps | 8 Mb/s | For busier Wi-Fi |
| 1080p30 | 1920×1080, 30 fps | 6 Mb/s | For weak Wi-Fi |
| 720p60 | 1280×720, 60 fps | 5 Mb/s | Smallest; for the weakest connections |
| 4K30 | 3840×2160, 30 fps | 20 Mb/s | Sharper; about 0.2 s more delay; needs strong 5 GHz Wi-Fi and hardware HEVC decoding |
| 4K60 | 3840×2160, 60 fps | 30 Mb/s | Sharpest; about 0.2 s more delay; needs strong 5 GHz Wi-Fi and hardware HEVC decoding |
| Portrait 1080p60 | to be settled by V-4 | 12 Mb/s | For vertical scenes |

## Beautify properties

The filter's properties follow the same split (UI-4). An "Advanced settings" switch shows or hides the advanced rows, using OBS's modified-callback mechanism:

| Property | Type | Mode |
| --- | --- | --- |
| Style | List: Natural, Soft, Glam, the user's styles | Simple |
| Beauty | Slider 0–100 | Simple |
| Advanced settings | Switch | Simple |
| Smoothing, Texture, Tone evening, Sharpening, Glow, Mask softness, Detail size | Sliders 0–100 | Advanced |
| Show mask | Switch | Advanced |
| Save as style…, Delete style | Buttons | Advanced |

Moving an advanced slider turns the style into "Custom"; "Save as style…" names it.

## Add iPhone Camera wizard

A `QWizard`, like OBS's own Auto-Configuration Wizard.

1. **Prepare your iPhone** (SET-1, SET-2). Numbered steps with the annotated screenshots from [setup.md](setup.md#2-prepare-the-iphone-once), which carry numbers and arrows but no words, so one set serves English and Russian:
   1. Install Blackmagic Camera from the App Store (with a QR code to the store page).
   2. Turn on its HTTP server: ① Settings tab, ② HTTP Server, ③ Enable HTTP Server.
   3. Allow Local Network access when iOS asks.
   4. Keep the app open on screen; a charger is recommended.

   At the bottom the page shows a live "Looking for phones…" line. When exactly one phone appears, the wizard moves on by itself; with several it moves to page 2.
2. **Choose your phone.** Name, model, app version and address of each phone found, plus "Enter address manually".
3. **Picture.** Stream preset, look, Beauty style and strength (or off), and "Set up the camera for streaming" (on by default, CTL-8). Each choice has its tooltip; nothing else is asked.
4. **Connecting.** A checklist that fills in as it goes:
   - Saving your current phone settings (the [before snapshot](architecture.md#reset-and-restore))
   - Setting up the stream
   - Waiting for the picture

   On an error the step shows the cause and fix, and a Try again button.

Finish adds the source to the current scene fitted to the canvas, attaches Beautify, and shows the Camera Controls dock if it was hidden (ONE-1).

## Remote Control dialog

Laid out like Tools → WebSocket Server Settings (WEB-5):

- Enable Remote Control (off by default)
- Server port (4466)
- Enable authentication, password (generated; Show, Copy, Generate)
- Show Connect Info: the panel address (the computer's local address), the password, and a QR code that opens the panel already signed in
- A note: "Only devices on your local network can connect."

## Web panel

- One page served by OBS. It works on a phone held upright and on a tablet, with touch-sized sliders.
- Simple and Advanced like the dock (UI-4). Simple is the default and shows the same eight rows; Advanced adds the tabs. Everything is rendered from `GET /api/v1/controls`, so the panel always matches the dock. Updates arrive over the WebSocket (WEB-2).
- Every control has an ⓘ button that opens its help text (UI-5).
- Colors follow the current OBS theme: the server passes the Qt palette at `/api/v1/theme`, and the panel maps it to CSS variables. Text uses the device's system font.
- Language follows OBS's language and uses the same translations as the dock.
- Sign-in: a password prompt, or the QR code from the dialog.

## Text and languages

- All text lives in `data/locale/en-US.ini` and `ru-RU.ini` (NFR-5). The panel gets the same strings from the API, so there is one translation.
- Messages are short, plain and say what to do. Units are K, mm, Mb/s, fps and %.
