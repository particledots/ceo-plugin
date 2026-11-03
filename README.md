# Ceò (working build name: Ceo)

An original AU / VST3 granular effect for Logic Pro, inspired by the feel of a granular
Eurorack module. All DSP is our own code; it is not a port or copy of anything.

Status: **milestone 4 (awaiting testing in Logic).** Six layers α to ζ (10 s plus a 3 s tail
each) and Ω, which joins all six end to end along Scan. Capture records into the selected layer
(in Ω, the layer under the Scan knob); Dub decides how much of the old audio is kept (0 = replace,
centre = half, full = sound on sound). Erase and Undo (one step) are plain buttons: every click
counts. Strike fires one grain per click, 15% louder than continuous grains, even with Intensity
at either end. Pitch deviation: random free spread one way, random whole-semitone steps the other
way. Follow mode (a plain on/off button): Scan becomes a speed control for a playhead that moves
through the layer by itself; Capture or Strike restarts it. Still generic sliders.
Not yet working: Mod, Hold, Sense, onset detection, Wavetable mode, mono/stereo input choice
(milestone 5), styled panel (6), saving recordings with the project (7).

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
