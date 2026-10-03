# Remote Control API

The HTTP API that obs-bmagicam serves when Remote Control is on (WEB-1 to WEB-5). The web panel uses exactly this API, so anything the panel can do, a script, Stream Deck or Bitfocus Companion can do too (WEB-3). How the server is built is in [architecture.md](architecture.md#remote-control-server).

## Basics

- Turn it on in Tools → iPhone Camera Remote Control. Default address: `http://<computer>:4466`.
- `GET /` serves the web panel. The API lives under `/api/v1`.
- JSON in UTF-8. Changes within v1 only add fields and endpoints; anything incompatible becomes `/api/v2`.
- Only loopback and private network addresses may connect; others get 403 (WEB-4).

## Authentication

With authentication on (the default), every API request carries the password:

```
Authorization: Bearer <password>
```

A wrong or missing password returns 401 after a one-second delay. Ten failures within a minute block the address for a minute. The WebSocket authenticates with its first message ([Events](#events)).

## Errors

```json
{"error": {"code": "locked", "message": "Controlled by auto exposure. Set Auto exposure to Off to adjust ISO."}}
```

| Status | `code` | When |
| --- | --- | --- |
| 400 | `invalid` | Malformed request or value out of range |
| 401 | `unauthorized` | Missing or wrong password |
| 403 | `forbidden` | Address outside the local network |
| 404 | `not_found` | Unknown camera, control, look or filter |
| 409 | `locked` | Another setting locks the control; `message` says which |
| 422 | `unsupported` | The phone does not support this control |
| 503 | `offline` | The camera is not connected; the body includes its state |

## Identifiers

- **Camera:** the OBS source UUID of the iPhone Camera source. It stays the same across OBS restarts.
- **Control:** the IDs in the [control map](ui.md#control-map), for example `wb.temperature`, `color.saturation`, `focus.mode`.
- **Look:** `natural`, `studio`, `warm`, `vivid`, `soft`, `cinematic`, or the generated ID of a user look.

## Endpoints

### Plugin

| Method and path | Returns |
| --- | --- |
| `GET /api/v1/info` | Plugin, OBS and API versions, OBS language |
| `GET /api/v1/theme` | The current OBS theme's palette (window, base, text, button, highlight colors), so a client can match OBS |
| `GET /api/v1/locale` | UI strings in OBS's language |
| `GET /api/v1/controls` | Control descriptors: ID, tab, group, label, tooltip, kind, unit, whether it changes the stream, whether it shows in Simple mode |
| `GET /api/v1/stream-presets` | Stream presets ([ui.md](ui.md#iphone-camera-properties)) |

### Cameras

| Method and path | Body | Does |
| --- | --- | --- |
| `GET /api/v1/cameras` | — | All iPhone Camera sources: ID, source name, phone (name, model, app version, address), state, stats |
| `GET /api/v1/cameras/{camera}` | — | One camera with every control's value, availability, lock and range or options |
| `PUT /api/v1/cameras/{camera}/controls/{control}` | `{"value": 4600}` | Changes a control. Answers 202 at once; the confirmed value arrives as an event. With `?wait=1` it answers 200 once the phone confirms, or 504 after 3 s |
| `POST /api/v1/cameras/{camera}/actions/{action}` | depends | Runs an action, see below |
| `PUT /api/v1/cameras/{camera}/look` | `{"look": "studio"}` | Applies a look |
| `PUT /api/v1/cameras/{camera}/stream` | `{"preset": "1080p60-high"}` | Changes the stream preset |

Actions:

| Action | Body | Does |
| --- | --- | --- |
| `wb-auto` | — | One-shot auto white balance |
| `refocus` | — | Autofocus once |
| `focus-point` | `{"x": 0.5, "y": 0.4}` | Focus at a point of the picture, 0–1 from the top left |
| `set-up-for-streaming` | — | CTL-8 |
| `record-start`, `record-stop` | — | Recording on the phone |
| `reset-group` | `{"group": "color"}` | Resets one group to defaults (RST-3) |
| `reset-defaults` | — | Reset to camera defaults (RST-1) |
| `restore-settings` | — | Restore the phone's settings from before obs-bmagicam (RST-2) |

Resets answer 202 and report progress as events until every value is confirmed (RST-4).

### Looks

| Method and path | Body | Does |
| --- | --- | --- |
| `GET /api/v1/looks` | — | Built-in and user looks with their values |
| `POST /api/v1/looks` | `{"name": "Evening", "camera": "<camera>"}` | Saves a camera's current color as a user look |
| `PATCH /api/v1/looks/{look}` | `{"name": "Evening warm"}` | Renames a user look |
| `DELETE /api/v1/looks/{look}` | — | Deletes a user look |

### Beautify

| Method and path | Body | Does |
| --- | --- | --- |
| `GET /api/v1/beautify` | — | Every Beautify filter: source, filter name, enabled, style, strength, advanced values |
| `PUT /api/v1/beautify/{source}/{filter}` | `{"enabled": true, "style": "soft", "strength": 60}` or advanced values such as `{"smoothing": 45}` | Changes a filter; fields left out keep their value |
| `GET /api/v1/beautify/styles` | — | Built-in and user styles |

Advanced values: `smoothing`, `texture`, `evening`, `sharpen`, `glow`, `maskSoftness`, `detailSize` (0–100) and `showMask`. Setting one turns the style into `custom`, as in the dock.

`{source}` is the OBS source UUID and `{filter}` the filter name, URL-encoded. The Stabilize filter, if built ([architecture.md](architecture.md#stabilization)), gets the same pair of endpoints under `/api/v1/stabilize`.

## Events

`GET /api/v1/events` upgrades to a WebSocket. The first client message authenticates:

```json
{"op": "hello", "password": "…"}
```

The server answers with everything a client needs to draw its UI:

```json
{"op": "ready", "cameras": [ … ], "looks": [ … ], "beautify": [ … ]}
```

Then it pushes changes as they happen:

```json
{"op": "control", "camera": "8f1c…", "control": "wb.temperature", "value": 4600, "locked": false}
{"op": "camera",  "camera": "8f1c…", "state": "live", "text": "Live · 1080p60 · 12 Mb/s"}
{"op": "stats",   "camera": "8f1c…", "fps": 60, "bitrateKbps": 12100, "phoneBufferPercent": 3, "battery": 50}
{"op": "reset",   "camera": "8f1c…", "done": 14, "total": 31}
{"op": "beautify", "source": "2b7e…", "filter": "Beautify", "enabled": true, "settings": { … }}
{"op": "looks", "looks": [ … ]}
```

Clients can also send changes over the socket. The panel does this while a slider is dragged; it behaves like the matching PUT or POST:

```json
{"op": "set", "camera": "8f1c…", "control": "color.saturation", "value": 1.2}
{"op": "action", "camera": "8f1c…", "action": "refocus"}
```

## Examples

```sh
H='Authorization: Bearer <password>'
B=http://192.168.1.20:4466/api/v1

curl -H "$H" $B/cameras
curl -H "$H" -X PUT $B/cameras/8f1c…/look -d '{"look":"vivid"}'
curl -H "$H" -X PUT "$B/cameras/8f1c…/controls/wb.temperature?wait=1" -d '{"value":5200}'
curl -H "$H" -X POST $B/cameras/8f1c…/actions/refocus
```

For Bitfocus Companion or Stream Deck, use their generic HTTP actions with the same URLs, the `Authorization` header and a JSON body.
