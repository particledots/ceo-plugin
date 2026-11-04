# Ceò (working build name: Ceo)

An original AU / VST3 granular effect for Logic Pro, inspired by the feel of a granular
Eurorack module. All DSP is our own code; it is not a port or copy of anything.

Status: **milestone 6 (awaiting testing in Logic).** New: the **styled panel** from the approved
mockup replaces the generic sliders: 15 knobs (double-click returns a knob to its default; knobs
that default to the centre click into the centre), the round **layer dial** (click an indicator,
drag round the dial, or use the mouse wheel), **Capture** and **Strike** buttons, **Erase**, **Undo**,
**Onset** and **Follow** buttons, and the **Mod target** and **Input mode** dropdowns. The **stream
display** shows the selected layer as a ribbon (thickness = loudness), the grains the engine really
starts as specks (scatter grows with Spray), a dashed play line, and the Scan marker (the playhead in
Follow mode); Omega shades through all six layer colours. Two small lights above Grain direction show
backwards and forwards grains. The window can be resized (drag the bottom-right corner). Every
parameter id is unchanged. Also: a restart of playback no longer cuts grains, the oscillator and the
reverb/echo tails dead (the likely cause of the small click on pressing play).

Earlier milestones: six layers α to ζ (10 s plus a 3 s tail each) and Ω; Capture, Dub, Erase, Undo,
Strike, Pitch deviation, Follow mode, Onset capture with Hold and Sense, Mono/Stereo input, the Mod
section (Pan, Hold, Reverb, Delay) and Wavetable mode.
Not yet working: saving recordings with the project and loading your own audio (milestone 7).

## Folders

| Folder | What it is |
|---|---|
| `engine/` | The audio engine. Plain C++, no plugin code. This is the part reused for later plugins. |
| `plugin/` | The plugin wrapper: what Logic sees, the list of controls, and the styled panel (`PluginEditor`, `CeoControls`, `CeoLookAndFeel`, `StreamDisplay`). |
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
