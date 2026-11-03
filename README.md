# Ceò (working build name: Ceo)

An original AU / VST3 granular effect for Logic Pro, inspired by the feel of a granular
Eurorack module. All DSP is our own code; it is not a port or copy of anything.

Status: **milestone 3, first sound (awaiting testing in Logic).** Press Capture to record
up to 10 s (plus a 3 s tail) into layer α, release it to stop, then turn Dry/wet up: grains
play from the recording, shaped by Length, Scan, Spray, Intensity, Grain window, Grain
direction and Pitch. Input level, Dry/wet and Output level work. Still generic sliders.
Not yet working: Strike, Pitch deviation, layers β to Ω, Dub, Erase/Undo, Follow mode,
Mod, Hold, Sense, Wavetable mode (milestones 4 and 5).

## Folders

| Folder | What it is |
|---|---|
| `engine/` | The audio engine. Plain C++, no plugin code. This is the part reused for later plugins. |
| `plugin/` | The plugin wrapper: what Logic sees, the list of controls, the generic sliders. |
| `.github/workflows/` | The automatic build. |

The wrapper hands the engine the control values and the audio; the engine hands audio back.
Nothing in `engine/` knows about sliders or Logic.

## Getting the plugin

1. Open the **Actions** tab of this repo and click the latest **Build Ceo** run.
2. At the bottom, under **Artifacts**, download **Ceo-macOS** and unzip it (twice: the download is a zip containing `Ceo-macOS.zip`).
3. Inside the `Ceo-macOS` folder are `Ceo.component`, `Ceo.vst3`, `INSTALL.txt`, `auval.log` and
   `Install-Unofficial-Plugin.command`.
4. In Terminal, once, clear the download block: `xattr -dr com.apple.quarantine ~/Downloads/Ceo-macOS/*`
   (adjust the path if you unzipped elsewhere).
5. **Easy install (Logic, Audio Unit):** double-click `Install-Unofficial-Plugin.command`. It asks for your Mac
   password once, installs the AU for all users and restarts the AU registrar.
   Or by hand: copy `Ceo.component` to `~/Library/Audio/Plug-Ins/Components/` and `Ceo.vst3` to `~/Library/Audio/Plug-Ins/VST3/`.
6. Restart Logic. If it was already scanned, Logic Pro > Settings > Plug-In Manager > Reset & Rescan Selection.

The plugin appears under **Audio Units > particledots > Ceo**.

`tools/install/Install-Unofficial-Plugin.command` is the same generic installer used in the other plugins.
Do not edit it here; it finds any `.component` next to it, so it needs no changes.

## Notes

- Built as a universal binary (Apple Silicon and Intel), macOS 11 or newer.
- Signed ad-hoc only (no Apple Developer account), hence the quarantine step above.
- The plugin is named "Ceo" without the accent for now, to keep the first build as safe as
  possible. The accented name can be tried once the skeleton is proven.
