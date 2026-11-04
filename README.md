# Ceo

An original AU / VST3 granular effect for Logic Pro, inspired by the feel of a granular
Eurorack module. All DSP is our own code; it is not a port or copy of anything.

Status: **milestone 9 (polish, awaiting testing in Logic).** The panel title now reads "Ceo" (the plugin's name in Logic was
already "Ceo"; its internal name, codes and bundle id are unchanged, so saved projects still find it). The CPU use of the
engine was measured in worst cases (all six layers, longest grains, spray, maximum reverb, Strike spam, 88 grains):
about 12% of one core on average and at most about a third of a block's time for the worst single block, so the engine
was left exactly as it was. The build now also runs pluginval on the AU and the VST3 (results are in the package as
`pluginval-AU.log` and `pluginval-VST3.log`).

Milestone 8 (tested): **Pitch Snap** and **Pitch Scale**.

- **Pitch Snap** (menu under **Pitch**: Off / Steps / Semitones). Steps makes the knob land on octaves and fifths:
  the list is 0, 7, 12, 19, 24, 31, 36, 43, 48 semitones up and down, but this knob only travels +-24, so only
  0, 7, 12, 19 and 24 can be reached (nine resting places: -24 -19 -12 -7 0 +7 +12 +19 +24). Semitones lands on whole
  semitones. While you drag the knob, its value bubble shows the note that really plays (a whole number such as
  -12 or +7; with Snap Off, the smooth value to two decimals). The knob's pointer itself still moves smoothly.
  Wavetable mode follows Pitch Snap too.
- **Pitch Scale** (menu under **Pitch deviation**: Off + 28 scales, the same list as the other particledots
  plugins). With a scale on, the random jumps of Pitch deviation land only on notes of the scale, built on the
  Pitch knob's note (after Pitch Snap); there is no Key menu. Both sides of the knob then give whole-note jumps,
  1 to 12 semitones either way (just off centre only +-1, at the ends +-12), each note of the scale inside that
  reach equally likely, the Pitch knob's own note included.
- Both are **off by default**. With both Off the sound is exactly what it was before (checked sample by sample).
- Two new parameters, `pitchSnap` and `pitchScale`, added after the existing 24 (version 2, so the existing ones keep
  their places in the AU parameter list); every existing id is unchanged, and projects saved with earlier builds
  load with both menus Off.

Earlier milestones: the styled panel (milestones 6 and 7: squarer layout, round layer dial, stream display, Capture
switching itself off when the layer is full), six layers α to ζ (10 s plus a 3 s tail each) and Ω; Capture, Dub,
Erase, Undo, Strike, Pitch deviation, Follow mode, Onset capture with Hold and Sense, Mono/Stereo input, the Mod
section (Pan, Hold, Reverb, Delay) and Wavetable mode.
Not yet working: saving recordings with the project (milestone 10) and loading your own audio (milestone 11).

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
