# Ceò (working build name: Ceo)

An original AU / VST3 granular effect for Logic Pro, inspired by the feel of a granular
Eurorack module. All DSP is our own code; it is not a port or copy of anything.

Status: **milestone 2, project skeleton.** It loads in Logic as an audio effect, passes
audio through unchanged, and shows generic sliders for every control in the design spec.
There is no granular sound yet (that is milestone 3).

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
2. At the bottom, under **Artifacts**, download **Ceo-macOS** and unzip it.
3. Inside are `Ceo-AU.zip`, `Ceo-VST3.zip` and `auval.log`. Unzip the plugin zips.
4. Put `Ceo.component` in `~/Library/Audio/Plug-Ins/Components/` and (optional) `Ceo.vst3` in
   `~/Library/Audio/Plug-Ins/VST3/`.
5. In Terminal, once, remove the download quarantine flag:
   `xattr -cr ~/Library/Audio/Plug-Ins/Components/Ceo.component`
6. Restart Logic. If it was already scanned, Logic Pro > Settings > Plug-In Manager > Reset & Rescan Selection.

The plugin appears under **Audio Units > particledots > Ceo**.

## Notes

- Built as a universal binary (Apple Silicon and Intel), macOS 11 or newer.
- Signed ad-hoc only (no Apple Developer account), hence the quarantine step above.
- The plugin is named "Ceo" without the accent for now, to keep the first build as safe as
  possible. The accented name can be tried once the skeleton is proven.
