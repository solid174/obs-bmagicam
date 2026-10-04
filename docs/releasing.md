# Building and releasing

Every release ships for Windows, macOS and Linux at once (NFR-3). The build comes from the official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) and runs on GitHub Actions.

## Platforms and packages

| Platform | Toolchain | CI runner | Package |
| --- | --- | --- | --- |
| Windows 10 and 11, x64 | Visual Studio 2022, CMake 3.30 | `windows-2022` | `.exe` installer and `.zip` |
| macOS 12 or newer, universal (Apple Silicon and Intel) | Xcode 26.5, CMake 3.30 | `macos-26` | `.pkg`, signed and notarized |
| Ubuntu 24.04, x86_64 | GCC 13, Ninja, CMake 3.28 | `ubuntu-24.04` | `.deb` |

The Windows installer is the one addition to the template's packaging: a small Inno Setup script, built in CI, that finds OBS and installs into `C:\ProgramData\obs-studio\plugins\obs-bmagicam`. The `.zip` holds the same files for installing by hand. Inno Setup is preinstalled on GitHub's Windows runners.

Linux packages target OBS from the official PPA or the distribution. OBS from Flatpak needs a Flatpak extension instead, which is not part of 1.0.

## Pinned versions

`buildspec.json` pins what the plugin builds against:

| Dependency | Version | Why |
| --- | --- | --- |
| obs-studio sources (headers) | 32.2.0 | The oldest supported OBS. Building against it keeps the plugin loadable on every later 32.x and 33.x |
| obs-deps prebuilt | 2026-07-15 | What OBS 32.2 ships: FFmpeg 8.1.2, Mbed TLS, nlohmann/json, qrcodegen |
| obs-deps Qt 6 | 2026-07-15 | The Qt that OBS 32.2 ships (6.11) |

cpp-httplib is fetched with CMake `FetchContent` from a release URL with a SHA-256 hash, so a build never picks up an unreviewed version.

On Ubuntu the same libraries come from the distribution: `obs-studio` (PPA), `libavcodec-dev`, `libavformat-dev`, `libavutil-dev`, `libmbedtls-dev`, `nlohmann-json3-dev`, `libqrcodegencpp-dev`, `libavahi-client-dev` and `qt6-base-dev`. CI checks that the distribution's FFmpeg has SRT built in: `ffprobe -protocols | grep -qx '  srt'`.

On Ubuntu the plugin builds against OBS from the PPA and the distribution's libraries, the same ones that OBS uses there: FFmpeg 6.1, Mbed TLS 2.28 and Qt 6.4. The code therefore sticks to APIs that exist in both those versions and the obs-deps ones.

When OBS moves to a new FFmpeg major ([architecture.md](architecture.md#ffmpeg-abi)), the pins move with it in a new plugin release. Until then, a mismatched combination does not run ([architecture.md](architecture.md#ffmpeg-abi)).

## Building locally

Presets come from the template. macOS needs Xcode 26.5 or newer, the version OBS 32.2 requires; the Command Line Tools alone are not enough, because the template builds with the Xcode generator.

```sh
cmake --preset macos         && cmake --build --preset macos
cmake --preset windows-x64   && cmake --build --preset windows-x64
cmake --preset ubuntu-x86_64 && cmake --build --preset ubuntu-x86_64
```

For a quick test on macOS, copy the built `obs-bmagicam.plugin` into `~/Library/Application Support/obs-studio/plugins/` and restart OBS. OBS's log (Help → Log Files) shows `[obs-bmagicam]` lines when the plugin loads.

## Continuous integration

The template's workflows stay as they are:

- `push.yaml` and `pr-pull.yaml` build every push and pull request on all three platforms and upload the packages as artifacts.
- `check-format.yaml` runs clang-format 19 on C++ and gersemi on CMake files.
- Pushing a semantic-version tag (`1.0.0`, `1.1.0-beta1`) on `main` creates a draft GitHub release with signed packages attached.

Added for this project:

- **Load test** (`load-test.yaml`) after every push build: on each platform it installs the package the way a user would (the `.deb`, the `.pkg` into the home folder, the `.zip` into ProgramData), starts the released OBS 32.2 with a fresh configuration, waits for "Startup complete" and checks OBS's log for the plugin's load message. GitHub's runners have no GPU, so OBS renders in software (Microsoft Basic Render Driver, Apple Software Renderer, Mesa llvmpipe), which is enough to load modules. A release is only drafted when the load test passes.
- **Unit tests** with CTest on all three runners, for the parts that need no phone and no OBS: Streaming XML generation, phone JSON mapping, the write coalescer, control descriptors, timestamp mapping, look files.
- **Windows installer** built after the `.zip`.
- **License notices** collected into every package (LIC-3).
- **Ubuntu SRT check**, as above.

macOS signing and notarization use the template's repository secrets:

| Secret | Holds |
| --- | --- |
| `MACOS_SIGNING_APPLICATION_IDENTITY` | "Developer ID Application: …" |
| `MACOS_SIGNING_INSTALLER_IDENTITY` | "Developer ID Installer: …" |
| `MACOS_SIGNING_CERT`, `MACOS_SIGNING_CERT_PASSWORD` | The certificates as base64 `.p12`, and its password |
| `MACOS_KEYCHAIN_PASSWORD` | Password for the temporary CI keychain |
| `MACOS_SIGNING_PROVISIONING_PROFILE` | Provisioning profile, if used |
| `MACOS_NOTARIZATION_USERNAME`, `MACOS_NOTARIZATION_PASSWORD` | Apple ID and app-specific password for notarization |

An Apple Developer Program membership is needed for these. Without it, CI still builds an unsigned `.pkg` that macOS only opens after a right-click → Open.

## Versions

- Semantic versioning. The version lives in `buildspec.json` and in the tag.
- `CHANGELOG.md` lists user-visible changes per release.
- Release notes state the supported OBS versions and the Blackmagic Camera version the release was tested with.

## Release test

Run on the release candidate packages, installed from the draft release on clean machines:

| Area | Check | Where |
| --- | --- | --- |
| Install | Installs, OBS loads it, Tools entries present, uninstalls cleanly | All three |
| Discovery | Phone listed by name within 5 s; manual address works | All three |
| Stream | 1080p60 for 30 min: no dropped or lagged frames in OBS's stats, audio in sync | All three |
| Latency | Glass to glass: the phone films a millisecond clock on the OBS computer's screen next to OBS's preview of it (NFR-2) | All three |
| Lip sync | Clap test in an OBS recording at the start and after two hours: audio within 40 ms ahead and 60 ms behind the picture; again after a reconnect (NFR-6) | All three |
| Microphone sync | With a USB microphone: Sync while talking, then a clap test in a recording: the microphone within 40 ms ahead and 60 ms behind the iPhone's picture; Undo restores the offset (SYN-1 to SYN-3) | All three |
| 4K presets | 4K30 and 4K60 stream for 10 min without drops; the extra delay matches the preset's description | All three |
| Reconnect | Wi-Fi off and on, app to background and back, phone locked and unlocked, the stream stopped and started on the phone: recovers without action, and the phone shows no alert (CAM-6) | All three |
| Hide and show | Stream stops 2 s after hiding and resumes on show (CAM-7) | One |
| Controls | Every row of the [control map](ui.md#control-map) changes the phone and the preview; changes on the phone appear in OBS (CTL-1 to CTL-3) | One, spot checks on the others |
| Looks | LOOK-5 on a color chart and real skin, checked on waveform and vectorscope | One |
| Resets | RST-1 to RST-4, including Wi-Fi pulled in the middle of a reset | One |
| Beautify | BEA-1 to BEA-8, GPU time within NFR-1 | Windows on Iris Xe, macOS on M1 |
| Stabilization | Off and On in the dock switch the app between Off and Standard; Cinematic and Extreme chosen on the phone show as On; each mode reaches the stream and matches the app's own picture (STB-1, STB-2) | One |
| Simple mode | Someone who has never used the plugin installs it, connects a phone and gets a good picture with Simple mode and the tooltips only, without help (UI-4, UI-5) | One |
| Remote Control | Panel in Safari on iPhone and Chrome on Android; API examples; password lockout; outside address refused | All three |
| Themes | Dock, properties, wizard and dialog in every built-in theme; switch theme while they are open (UI-1) | All three |
| Languages | English and Russian, no clipped text (NFR-5) | One |
| Several phones | Two phones live at once (CAM-8) | One |

## Release checklist

1. The release test passes.
2. `CHANGELOG.md` is updated and the version bumped in `buildspec.json`.
3. Tag `x.y.z` on `main`. CI builds, signs, notarizes and drafts the release.
4. Install each package from the draft on a clean machine and smoke-test it: find the phone, go live, change a look.
5. Write the release notes: supported OBS versions, tested app version, known issues.
6. Publish.

## Licenses

The plugin is GPL-2.0-or-later (LIC-1). Packages contain `THIRD-PARTY-NOTICES.txt` with the license of every piece of code compiled into the plugin (cpp-httplib, MIT) and of any library the package itself carries. Libraries the plugin uses from OBS ship with OBS.
