# Blackmagic Camera API

The part of the Blackmagic Camera app's remote-control API that obs-bmagicam uses, as verified against a real phone. This is the reference for the plugin's phone client. Where the phone and Blackmagic's documentation disagree, the phone wins and the difference is listed under [Quirks](#quirks).

| Verified on | |
| --- | --- |
| Phone | iPhone 17 Pro |
| App | Blackmagic Camera 3.5.100017 (API version 1.2.0) |
| Network | PC on the iPhone's Personal Hotspot |
| Receiver | OBS Studio 32.2.2 on macOS, and a bare libsrt receiver |
| Date | 2026-10-03 |

Sources:

- The phone serves its own OpenAPI 3.0.1 specs (16 YAML files) and an AsyncAPI spec for the WebSocket at `https://<phone>:4444/control/documentation.html`. They are the most complete description and include endpoints Blackmagic's PDF does not have.
- [REST API for Blackmagic Cameras](https://documents.blackmagicdesign.com/DeveloperManuals/RESTAPIforBlackmagicCameras.pdf) (August 2025) predates the app's API. Use it for background only.
- [Blackmagic Streaming XML File Format](https://documents.blackmagicdesign.com/DeveloperManuals/StreamingXMLFileFormat.pdf) (January 2026) for custom livestream destinations.

The API appeared in Blackmagic Camera 3.4 for iOS and for Android (July 2026). Only iOS has been tested.

## Availability

- The server is turned on in the app under Settings → Network Access → HTTP Server → Enable HTTP Server ([setup.md](setup.md#2-prepare-the-iphone-once) has annotated screenshots).
- Settings → Remote Camera Control → Camera Available for showed "Control and Monitor" while the API reported `control-and-monitor` (see below). Whether the Remote Password on the same page protects the API is V-17.
- The server runs only while the app is open on screen. It stops when the phone locks or the app goes to the background, and comes back when the app returns.
- After a video format change the server may stop answering for a few seconds. Clients must retry.
- `GET /access/status` returns `{"availability":"control-and-monitor"}`. Other values (for example monitor-only) have not been seen yet; see V-7 in [architecture.md](architecture.md#verification-items).

## Discovery

The app advertises itself over Bonjour (mDNS/DNS-SD):

| Field | Value |
| --- | --- |
| Service type | `_http._tcp` (although the server speaks HTTPS) |
| Instance name | The device UUID, for example `5F1E2C3A-0B4D-4E6F-8A9B-1C2D3E4F5A6B` |
| Host | `<phone-name>.local` |
| Port | `4444` |

TXT record:

| Key | Example | Use |
| --- | --- | --- |
| `device_id`, `unique id` | `5F1E2C3A-…` | Stable identity of the phone. The plugin stores this, not the IP address |
| `capabilities` | `cameraControl` | Filter: only instances with this value are Blackmagic Camera phones |
| `path` | `/control/api/v1` | REST base path |
| `ws` | `/control/api/v1/event/websocket` | WebSocket path |
| `camera name` | `A` | Camera letter set in the app (shown in lists) |
| `device name`, `model` | `iPhone 17 Pro` | Shown in lists |
| `version` | `3.5.100017` | App version |
| `port` | `4444` | Same as the SRV port |
| `proto`, `txtvers` | `1.0`, `1` | Protocol and TXT versions |
| `connected device` | empty | Probably the controller currently connected; see V-8 |

## Transport

- **HTTPS only, port 4444.** Plain HTTP gets an empty reply. TLS 1.2 and 1.3 both work.
- **Self-signed certificate**, issuer `O=Blackmagic Design`. Clients must not verify it against a CA.
- **No authentication.** Anyone on the network can control the phone.
- **`Connection: close` on every response.** Each request is a new TCP and TLS handshake. A request took 35–110 ms round trip over the hotspot.
- `Access-Control-Allow-Origin: *`. A browser could call the phone directly, but would first have to accept the self-signed certificate, so the plugin's web panel goes through OBS instead.
- Base path `/control/api/v1`, JSON bodies, `Content-Type: application/json`.
- Writes answer `204 No Content`. Exception: livestream start and stop answer `200` with a JSON boolean.

Status codes:

| Code | Meaning on the phone | Plugin reaction |
| --- | --- | --- |
| 200, 204 | Done | — |
| 400 | Value out of range or malformed | Revert the control, log |
| 403 | Locked in the current state, for example ISO while auto exposure is on | Show the lock reason (CTL-4) |
| 404 | Not supported on this device, or no such item | On a GET at connect time: hide the control |
| 409 | Not possible in the current state | Retry once after state refresh, then report |
| 501 | Not implemented | Hide the control |

## WebSocket

`wss://<phone>:4444/control/api/v1/event/websocket`, same certificate as REST. Connecting took about 290 ms. It reports changes only; all writes go through REST.

On connect the phone sends:

```json
{"type":"event","data":{"action":"websocketOpened"}}
```

Subscribe:

```json
{"type":"request","id":2,"data":{"action":"subscribe","properties":["/video/iso","/lens/focus"]}}
```

The response carries the current value of every subscribed property:

```json
{"type":"response","id":2,"data":{"action":"subscribe","properties":["/video/iso","/lens/focus"],
 "values":{"/video/iso":{"iso":2623},"/lens/focus":{"normalised":0.486}}}}
```

After that the phone pushes one event per change. It also sends one event per property right after subscribing:

```json
{"type":"event","data":{"action":"propertyValueChanged","property":"/video/iso","value":{"iso":2623}}}
```

Other actions: `unsubscribe`, `listSubscriptions`, `listProperties`. `listProperties` (and `GET /event/list`) returned 121 properties, including `/video/*`, `/lens/*`, `/colorCorrection/*`, `/audio/channel/{0,1}/*`, `/livestreams/*`, `/presets*`, `/camera/*`, `/media/*`, `/access/status` and `/monitoring/{LCD,HDMI}/*`.

While auto exposure is on, ISO and shutter change continuously, so their events can be frequent. The plugin throttles UI updates, not subscriptions.

## Endpoints

Values in the "Observed" column are from the test phone with the front ultra-wide camera active. Ranges and lists differ per phone model, lens and video format, so the plugin always reads them from the phone and never hard-codes them.

### Exposure and white balance

| Endpoint | Fields | Observed |
| --- | --- | --- |
| `GET/PUT /video/autoExposure` | `mode`: `Off`, `Continuous`, `OneShot`; `type` | `{"mode":"Continuous"}` |
| `GET/PUT /video/iso` | `iso` | 1920; under auto exposure also values outside the list, such as 2623 |
| `GET /video/supportedISOs` | `supportedISOs[]` | 20, 25, 32, 40, 50, 64, 80, 100 … 1600, 1920 |
| `GET/PUT /video/shutter` | `shutterSpeed` (1/x s) or `shutterAngle`; `continuousShutterAutoExposure` | `{"shutterSpeed":60,"continuousShutterAutoExposure":true}` |
| `GET/PUT /video/shutter/measurement` | `ShutterSpeed`, `ShutterAngle` | `ShutterSpeed` |
| `GET /video/supportedShutters` | `shutterSpeeds[]`, `shutterAngles[]` | 60 … 8000; 360 … 2.7 |
| `GET /video/flickerFreeShutters` | same | 1/100 and 216° at 60 fps |
| `GET/PUT /video/whiteBalance` | `whiteBalance` (K) | 4500 |
| `GET /video/whiteBalance/description` | `min`, `max` | 2500–10000 |
| `PUT /video/whiteBalance/doAuto` | — | One-shot auto white balance |
| `GET/PUT /video/whiteBalanceTint` | `whiteBalanceTint` | 15 |
| `GET /video/whiteBalanceTint/description` | `min`, `max` | −50…50 |

Not supported on iPhone (404): `/video/gain`, `/video/supportedGains`, `/video/ndFilter*`, `/video/detailSharpening*`.

### Lens and focus

| Endpoint | Fields | Observed |
| --- | --- | --- |
| `GET /lens/cameras` | `cameras[]`: `id`, `facing`, `focalLength`, `zoomFactor`, `isActive`, `isAvailable` | see below |
| `GET/PUT /lens/cameras/active` | `id` | `LensFrontUltraWide` |
| `GET/PUT /lens/cameras/auto` | `enabled`, `supported` | `supported: false` |
| `GET/PUT /lens/zoom` | `focalLength`, `normalized`; relative `adjustmentFocalLength`, `adjustmentNormalized` | 19 mm, 0 |
| `GET /lens/zoom/description` | `controllable`, `focalLength.min/max` | 19–570 mm (digital zoom beyond the lens) |
| `GET/PUT /lens/focus` | `normalised` (0–1); written as `normalised` only | 0.47 |
| `GET /lens/focus/description` | `controllable`, `capabilities.autoFocus` | controllable |
| `GET/PUT /lens/focus/autoFocus` | `enabled`, `mode`, read-only `state`, `errors[]`. `enabled: false` is manual focus | `{"enabled":true,"mode":"Continuous","state":"Idle"}` |
| `GET /lens/focus/autoFocus/description` | `supportedModes[]` | `OneShot`, `Continuous` (the spec also has `TrackObject`, `TrackFace`) |
| `GET/PUT /lens/focus/autoFocus/target` | `x`, `y`, optional `width`, `height` (0–1) | 0.5, 0.5 |
| `PUT /lens/focus/autoFocus/retrigger` | — | Refocus |
| `PUT /lens/focus/doAutoFocus` | `position.x`, `position.y` (0–1) | Tap-to-focus at a point |
| `GET/PUT /lens/opticalImageStabilization` | `enabled`: `false` is the app's stabilization Off. `true` reads for Standard, Cinematic and Extreme alike, and writing it sets Standard | true |
| `GET/PUT /lens/iris` | `apertureStop`, `normalized` | f/1.9, `controllable: false` |

Lenses on the iPhone 17 Pro:

| `id` | Facing | Focal length | Label |
| --- | --- | --- | --- |
| `LensFrontUltraWide` | front | 19 mm | 0.5× |
| `LensFront` | front | 19 mm | 0.5× |
| `Lens13mm` | back | 13 mm | 0.5× |
| `Lens24mm` | back | 24 mm | 1× |
| `LensWASecondary` | back | 48 mm | 2× |
| `Lens77mm` | back | 100 mm | 4× |
| `Lens200mm` | back | 200 mm | 8× |

Not supported: `/lens/virtual` (501).

### Color correction

All color correction applies to the livestream, so OBS sees it. Verified: setting saturation to 1.8 raised the average saturation of OBS frames from 0.355 to 0.541, and it went back to 0.352 after restoring 1.0.

Every field is optional in a PUT; omitted fields keep their value.

| Endpoint | Fields | Range | Neutral |
| --- | --- | --- | --- |
| `GET/PUT /colorCorrection/lift` | `red`, `green`, `blue`, `luma` | −2…2 | 0 |
| `GET/PUT /colorCorrection/gamma` | same | −4…4 | 0 |
| `GET/PUT /colorCorrection/gain` | same | 0…16 | 1 |
| `GET/PUT /colorCorrection/offset` | same | −8…8 | 0 |
| `GET/PUT /colorCorrection/contrast` | `pivot`, `adjust` | 0…1, 0…2 | 0.5, 1 |
| `GET/PUT /colorCorrection/color` | `hue`, `saturation` | −1…1, 0…2 | 0, 1 |
| `GET/PUT /colorCorrection/lumaContribution` | `lumaContribution` | 0…1 | 1 |

### Video format

The livestream follows the camera's video format (see [The stream](#the-stream)), so changing it changes what OBS receives.

| Endpoint | Fields | Observed |
| --- | --- | --- |
| `GET/PUT /system/videoFormat` | `name`, e.g. `1920x1080p60` | PUT answered 204 and took effect at once |
| `GET /system/supportedVideoFormats` | `videoFormats[]` | Landscape: 3840×2160, 1920×1080, 1280×720, 4032×3024 at 23.98–60 fps. In vertical mode only portrait sizes (1214×2160, 606×1080, 404×720, 1700×3024) |
| `GET/PUT /system/format` | `codec`, `frameRate`, `recordResolution`, `offSpeedEnabled`, `offSpeedFrameRate`, `resolutionDescriptor` | `HEVC (H.265):High`, 60, off-speed 4–60 |
| `GET /system/supportedFormats` | per resolution: `codecs[]`, `frameRates[]` | — |
| `GET/PUT /system/codecFormat` | `codec`, `container` | `HEVC (H.265)`, `mov` |
| `GET /system/supportedCodecFormats` | `codecFormats[]` | ProRes RAW HQ and ProRes RAW (Max…Low), ProRes 422 HQ/422/LT/Proxy, HEVC and H.264 (Max/High/Medium/Low) |
| `GET/PUT /system/dynamicRange` | `dynamicRange` | `Video`; supported `Video`, `Extended Video`, `Film`, `HLG` |
| `GET/PUT /system/audioCodec` | `audioCodec` | `AAC` |
| `GET /system/product` | `productName`, `deviceName`, `softwareVersion` | `iPhone 17 Pro`, `A`, `3.5.100017` |

### Phone screen (monitoring)

These change only what the phone shows on its own screen, not the stream (CTL-7).

| Endpoint | Fields | Observed |
| --- | --- | --- |
| `GET /monitoring/display` | `displays[]` | `["Device"]` |
| `GET/PUT /monitoring/Device/{zebra, focusAssist, falseColor, frameGuide, frameGrids, safeArea, displayLUT}` | `enabled` | all off except `displayLUT` |
| `GET/PUT /monitoring/Device/brightness` | `brightness` (0–100), `adjustable` | 80, adjustable. Dimming the phone screen saves battery and heat during long streams |
| `GET/PUT /monitoring/zebra` | `highlight.enabled`, `highlight.level` (75–100), `skinTone.enabled`, `skinTone.type` | off, 85 |
| `GET/PUT /monitoring/focusAssist` | `mode` (`Peak`, `ColoredLines`), `color`, `intensity` (0–100) | `ColoredLines`, Red, 85 |
| `GET/PUT /monitoring/frameGuideRatio` | `ratio` | `2:1` |
| `GET /monitoring/frameGuideRatio/presets` | `presets[]` | 2.4:1, 2.39:1, 2.35:1, 2:1, 1.85:1, 14:9, 4:3, 1:1, 4:5, 9:16, 2.76:1 |
| `GET/PUT /monitoring/frameGrids` | `frameGrids[]`: `Thirds`, `Crosshair`, `Dot`, `Horizon` | none. At most two, and one of two must be `Thirds` |
| `GET/PUT /monitoring/safeAreaPercent` | `percent` | 85 |

`/monitoring/Device/cleanFeed` answered 404.

### Audio

| Endpoint | Fields | Observed |
| --- | --- | --- |
| `GET /audio/channels` | `channels` | 2 |
| `GET /audio/supportedInputs` | list | `None`, `iPhone Microphone` |
| `GET/PUT /audio/channel/{n}/input` | `input` | `iPhone Microphone` |
| `GET /audio/channel/{n}/input/description` | `gainRange.Min/Max`, capabilities | gain 1…1 (not adjustable), no phantom power, low cut or pad |
| `GET/PUT /audio/channel/{n}/level` | `gain`, `normalized` | 1 |
| `GET/PUT /audio/channel/{n}/{phantomPower, padding, lowCutFilter}` | `enabled` | false; not supported with the built-in microphone |
| `GET /audio/channel/{n}/available` | `available` | true |

External microphones may add gain, low cut and phantom power. The plugin shows what `input/description` reports.

### Recording, phone status and presets

| Endpoint | Fields | Observed |
| --- | --- | --- |
| `GET /transports/0` | `mode`: `InputPreview`, `InputRecord` | `InputPreview` |
| `GET /transports/0/record`, `POST /transports/0/record`, `POST /transports/0/stop` | `recording`; optional `clipName` | Start and stop recording on the phone |
| `GET /transports/0/timecode` | `display`, `timeline` | — |
| `GET/PUT /transports/0/proxyRecording` | `enabled` | true |
| `GET /media/workingset` | `remainingSpace`, `remainingRecordTime` (s), `totalSpace` | 134 GB free, 17025 s |
| `GET /camera/power` | `source`, `batteries[].chargeRemainingPercent` | Battery, 50 % |
| `GET/PUT /camera/id` | `id` (0–255, CCU camera number) | 0 |
| `GET /camera/motionSensor/euler`, `/horizon` | roll, pitch, yaw | Available |
| `GET /presets` | `presets[]` (`.bmcpreset` files) | empty |
| `GET/PUT /presets/active` | `preset`: file name or `default` | `""` |
| `PUT /presets/{name}` | — | Saves the current camera state as a preset |
| `GET`, `DELETE /presets/{name}`, `POST /presets` | — | Download, delete, upload a preset file |
| `GET/PUT /slates/nextClip`, `/slates/takeAutoIncrement` | clip, lens and project metadata | — |

Not supported (501): `/camera/colorBars`, `/camera/programFeedDisplay`, `/camera/tallyStatus`, `/camera/timingReferenceLock`, `/transports/0/prerecord*`.

## Livestream to OBS

The phone has no "send video to this computer" call. It streams like it streams to YouTube: to a livestream destination. The plugin adds its own destination pointing at the PC, selects it and starts the stream. Verified end to end:

1. Upload a destination in [Blackmagic Streaming XML](https://documents.blackmagicdesign.com/DeveloperManuals/StreamingXMLFileFormat.pdf):

   ```
   PUT /livestreams/customPlatforms/obs-bmagicam.xml
   Content-Type: application/xml
   ```

   ```xml
   <?xml version="1.0" encoding="UTF-8"?>
   <streaming>
    <service>
     <name>OBS on STUDIO-PC</name>
     <servers>
      <server>
       <name>OBS</name>
       <url>srt://192.168.1.20:9710</url>
      </server>
     </servers>
     <profiles default="1080p60 High">
      <profile>
       <name>1080p60 High</name>
       <config resolution="1080p" fps="60" codec="H264">
        <bitrate>12000000</bitrate>
        <audio-bitrate>128000</audio-bitrate>
       </config>
      </profile>
     </profiles>
    </service>
   </streaming>
   ```

   Answer: 204. The phone stores the destination as **`<service name> SRT`** (here `OBS on STUDIO-PC SRT`), not under the file name. `GET` and `DELETE /livestreams/customPlatforms/{name}` take that name. The file name in the PUT is not used afterwards.

   Uploading again under a name the phone already has fails: the phone shows "Platforms Import Failed … This platform already exists" on its screen, yet answers 204. `GET` returns the stored copy in the phone's own layout (re-indented, with `low-latency`, `key` and `passphrase` added and `codec` and `audio-bitrate` dropped), and 404 when there is none. So the plugin compares the stored URL and profile bitrates and deletes an outdated copy before uploading.

2. Select it:

   ```
   PUT /livestreams/0/activePlatform
   {"platform":"OBS on STUDIO-PC SRT","server":"OBS","quality":"1080p60 High"}
   ```

   Answer: 204. `GET` returns the same plus `"url":"srt://192.168.1.20:9710"`. The bare service name was also accepted as `platform`. Selecting a destination while streaming, even the one already selected, restarts the stream.

3. `GET /livestreams/0/available` returns `{"available":true,"reasons":[]}`. Possible reasons: `not-supported`, `unsupported-format`, `in-playback`, `pending-format-transition`, `unexpected-reason`.

4. `PUT /livestreams/0/start` answers `200 true`. `/livestreams/0` goes `Idle` → `Connecting` → `Streaming`. The phone connected to the PC's SRT listener 155 ms after the request, and the first media packet arrived after 391 ms.

5. While streaming, `GET /livestreams/0` (or the WebSocket) reports `status`, `bitrate`, `duration`, `effectiveVideoFormat` and `cache` (send buffer use in %).

6. `PUT /livestreams/0/stop` answers `200 true`, and the status returns to `Idle` at once. The SRT connection is not always closed: the phone can stop sending and leave it open.

7. To undo: `PUT /livestreams/0/activePlatform` with the previously active destination, then `DELETE /livestreams/customPlatforms/OBS%20on%20STUDIO-PC%20SRT` (204). Both verified.

Built-in destinations on the test phone: `Blackmagic Cloud BMC`, `YouTube RTMP`, `Twitch RTMP`, `Vimeo RTMP`, `Vimeo SRT`. They report empty `profiles`.

The phone is the SRT caller; the PC listens. The PC must accept incoming UDP on the chosen port.

## The stream

As received from the phone over SRT:

| Property | Observed |
| --- | --- |
| Container | MPEG-TS |
| Video codec | HEVC Main, 8-bit 4:2:0, BT.709 limited range, while the camera recorded HEVC. The profile asked for `H264` and was ignored; see V-3 |
| Resolution | Follows the camera video format: `1920x1080p60` gave 1920×1080, `3840x2160p60` gave 3840×2160. In vertical mode (`1214x2160p60`) the stream was 1920×1080 landscape in two runs; in a third, starting the stream switched the camera to `3840x2160p60` and streamed 3840×2160 (V-4) |
| Frame rate | 60.00 fps, constant: 721 frames in 12.000 s, every frame 16.66–16.67 ms apart |
| GOP | Keyframe every 1.0 s, I and P frames only (no B-frames) |
| Bitrate | Follows the profile: 12 Mb/s requested, 11.7–12.4 Mb/s measured |
| Audio | AAC-LC, 48 kHz, stereo |
| Integrity | No transport stream errors in three 30 s captures; one damaged AAC frame in one of them |

Timing as the plugin's receiver saw it (two runs on the test hotspot, 40 s at 3840×2160 and 70 s at 1920×1080, both 12 Mb/s):

| Property | Observed |
| --- | --- |
| Audio against video | Relative to their timestamps, video arrives 15–35 ms later than audio, so video sets the receiver's delay (V-18) |
| Start of a stream | The phone first sends what it held before the connection: the first packet's timestamp was 1.8 s older than its arrival suggests, and about 1.8 s of frames came at once. The receiver skips them |
| Timestamps against the computer's clock | At 3840×2160, arrival ran 0.08–0.10 % ahead of the timestamps, in audio and video alike, so the phone's timestamps ran slow. At 1920×1080, −0.009 %, ordinary clock drift. The receiver follows both |
| Jitter | Within a second, packets arrive up to 20–35 ms apart relative to their timestamps |
| Decoding | HEVC 1920×1080 with VideoToolbox on the test Mac: 4.9 ms per frame including the copy to system memory, 5.7 ms at the 99th percentile |
| End of a stream | The last packet is cut off when the phone stops, so the receiver reads an error; it is the end of the stream |

## Latency

Method: OBS played 250 ms noise bursts through the Mac's speakers, and the phone's microphone picked them up. The start time is when OBS's audio meter registered the burst, so the numbers include OBS's monitoring output delay and the speaker. Arrival was measured two ways:

- **Packet arrival:** a bare libsrt receiver logged when each packet arrived, and the burst onset was located in the decoded audio.
- **OBS Media Source:** the time until the phone source's audio meter rose in OBS.

| Path | Camera format | SRT latency | Median (min–max), 8 bursts |
| --- | --- | --- | --- |
| Phone → PC, packet arrival | 1080p60 | 120 ms | 349 ms (318–368) |
| Phone → PC, packet arrival | 1080p60 | 50 ms | 347 ms (308–557) |
| Phone → PC, packet arrival | 2160p60 | 50 ms | 527 ms (501–572) |
| Phone → OBS Media Source (`ffmpeg_source`, buffering 0, hardware decoding) | 1080p60 | 120 ms | 1238 ms (1229–1446) |

Also measured: OBS's Media Source showed its first frame 2.2 s after the stream started.

What this means:

- About 350 ms passes before a 1080p60 packet reaches the PC, before any decoding. Lowering the PC's SRT latency from 120 to 50 ms did not reduce it, so the phone side dominates (V-1, V-2, V-11).
- 4K costs about 180 ms more than 1080p. The 4K stream presets say so.
- OBS's Media Source adds about 0.9 s on top. The plugin therefore has its own receiver; see [architecture.md](architecture.md#video-path).
- The audio path was measured, not the video path. Video latency is measured glass to glass in development (V-1).

## Quirks

- Bonjour advertises `_http._tcp`, but the server is HTTPS only.
- Every response closes the connection, so there is no keep-alive and every request costs a TLS handshake.
- iPhone answers 404, not 501, for most unsupported features.
- Responses contain `normalised` and sometimes also `normalized`. Writes must use `normalised`: on `/lens/focus`, `{"normalized": 0.25}` answered 500 ("The data couldn't be read because it is missing") and `{"normalised": 0.75}` 204 (V-10, 2026-10-04). The spec calls `normalised` deprecated all the same.
- The spec defines `shutterAngle` as degrees × 100 (18000 = 180°), but `/video/supportedShutters` returned plain degrees (172.8, 360). Check what PUT expects before using angles (V-9).
- `/monitoring/display` lists `Device`, while WebSocket property names use `LCD` and `HDMI`.
- Livestream start and stop answer `200 true`, not `204`.
- A custom destination is renamed to `<service name> SRT`, and later calls must use that name.
- The livestream ignored the profile's `codec` attribute and its resolution: it followed the camera's codec and video format. In vertical mode it arrived as 1920×1080 landscape, filling the frame, while `effectiveVideoFormat` said `1214x2160p60` (V-4).
- The server pauses for a few seconds after a video format change.
- Starting the livestream in vertical mode once switched the camera's video format to `3840x2160p60`, and it stayed so after the stream stopped. The plugin sets the format from the stream preset anyway, and its before snapshot keeps the user's format.
- The first request after a pause sometimes times out; the next one answers. The client retries.
- An upload under a destination name the phone already has fails with an alert on the phone's screen, but answers 204.
- A livestream that breaks reconnects by itself, usually within a second.
- After a restart (a stop and start, or selecting a destination while streaming), the phone's next SRT connection sometimes carries no media while `/livestreams/0` reports `Streaming` with bitrate 0. One such connection stayed open for about three minutes, so a receiver must close a connection that sends nothing.
- Right after a livestream restart, the phone's screen locked by itself (it had not been touched for a while), and the server stopped with it. The setup guide asks for Auto-Lock: Never.
- After the phone reconnected by itself, `/livestreams/0` kept reporting `Connecting` with frozen `duration` and `bitrate` while the stream arrived normally.
- A request with `Expect: 100-continue` is refused with 400 (`"Expect: 100-continue is not supported"`). HTTP libraries that add it to larger bodies, such as cpp-httplib above 1 KB, must be told not to.
