// Scenes: a folder of up to six WAV files (one per layer) plus a settings file. Modelled on the Arbhar's scene folders.
// No plugin framework, no audio code: this file only works out WHICH files and settings a Scene holds (and tests with real folders on Linux).
//
//  * scanFolder()      which WAV goes to which layer, and which .txt is the settings file.
//  * parsePresetText() reads an Arbhar "#ARB" preset file (tolerantly: only the PARAMETER: and PRESET_NAME: lines count, because real files
//                      have wrongly closed comment blocks) or Ceo's own "#CEO-SCENE" file, and says what it sets and what it ignores.
//  * decide()          what a load does (layers, settings, both or nothing), from the Load menu and the file's Load Configuration.
//  * nearestScale()    an Arbhar QuantiseTable -> the closest of Ceo's Pitch Scales.
#pragma once
#include <string>
#include <vector>

namespace ceo
{
namespace scene
{
    constexpr int kLayers = 6;

    // -1 = the file doesn't say. Values are Ceo's own parameter choice indexes.
    struct Settings
    {
        int inputMode = -1;      // 0 Mono, 1 Stereo
        int modTarget = -1;      // 0 Pan, 1 Hold, 2 Reverb, 3 Delay
        int follow = -1;         // 0 off (Scan), 1 on (Follow)
        int onsetCapture = -1;   // 0 off, 1 on
        int pitchSnap = -1;      // 0 Off, 1 Steps, 2 Semitones     (Ceo's own files only)
        int pitchScale = -1;     // 0 Off, 1.. = the 28 scales      (also set from an Arbhar QuantiseTable)
        bool any() const { return inputMode >= 0 || modTarget >= 0 || follow >= 0 || onsetCapture >= 0 || pitchSnap >= 0 || pitchScale >= 0; }
    };

    struct Preset
    {
        bool valid = false;                  // looked like a preset file
        bool ceoFile = false;                // Ceo's own file (else an Arbhar one)
        std::string name;                    // PRESET_NAME
        int loadConfiguration = -1;          // 0 nothing, 1 settings only, 2 layers only, 3 both; -1 not stated
        Settings s;
        std::vector<std::string> set;        // lines like "Input mode: Stereo"
        std::vector<std::string> ignored;    // Arbhar parameter names Ceo has no use for
        std::vector<std::string> notes;      // anything else worth telling the user
    };

    Preset parsePresetText (const std::string& text);
    Preset loadPresetFile (const std::string& file);        // reads the file (up to 2 MB), then parsePresetText

    struct SceneFiles
    {
        std::string folder;
        std::string name;                    // the folder's name
        std::string audio[kLayers];          // full path per layer, empty = no file for that layer
        std::vector<std::string> extra[kLayers];   // further files with the same leading digit (alphabetical), used only when joining is on
        int audioCount = 0;                  // layers that have a file
        int extraCount = 0;                  // files with a layer digit that come after the first one of their layer
        std::string settingsFile;            // full path, empty = none
        bool byDigit = false;                // layers were picked by the leading digit (else: first six in alphabetical order)
    };
    SceneFiles scanFolder (const std::string& folder);

    // The sub-folders of a folder (full paths, alphabetical), skipping hidden ones: the Scenes menu.
    std::vector<std::string> listScenes (const std::string& scenesFolder);

    enum LoadWhat { AsFileSays = 0, LayersOnly, SceneAll, SettingsOnly };
    struct Decision { bool layers = false; bool settings = false; std::string note; };
    Decision decide (LoadWhat what, const SceneFiles& files, const Preset& preset);

    // The Pitch Scale menu index (0 Off, 1..28) closest to an Arbhar table of semitone steps.
    int nearestScale (const std::vector<int>& semitones);

    // Ceo's own settings file (what SAVE SCENE writes).
    std::string writeCeoSettings (const std::string& name, const Settings& s);

    // names for reports
    const char* inputModeName (int i);
    const char* modTargetName (int i);
    const char* pitchSnapName (int i);
    std::string pitchScaleName (int i);
}
}
