// Ties each layer to a WAV file ("file link"), so a Logic project can bring its layers back without storing any audio.
// Ported from the Layers plugin of the same author (its LayerLibrary), adapted to Ceo's six layers (numbered 0..5 here).
// No JUCE (std::filesystem), so it is tested on Linux with real files. Every call is made from the main thread (or, under the mutex,
// from the host's state calls); the audio thread never touches it or any file: audio goes through the engine's hand-over slots.
//
//  * A layer that has been saved to / loaded from a file is "bound" to it: the library remembers the path (relative to the home folder when the
//    file is inside it), the file size and a fingerprint (hash of the file's bytes).
//  * The bindings are what a project stores (bindings() / restore()). On restore each bound layer is loaded from its file.
//    If the file isn't where it was, the home folder and its sub-folders are searched for a file with the same size and fingerprint.
//  * A binding is dropped when the user changes the layer (records over it, erases it). If a sample-rate change empties the layers,
//    the bound ones are loaded again from their files at the new rate.
#pragma once
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "ceo/Engine.h"
#include "ceo/LayerAudio.h"

namespace ceo
{

class LayerLibrary
{
public:
    enum Status { NotBound = 0, Waiting, Working, Ok, Changed, Missing, Failed };
    struct Binding { std::string path; uint64_t size = 0; uint64_t hash = 0; };   // path: relative to the home folder if inside it, else absolute
    struct LayerInfo
    {
        bool bound = false;
        bool unsaved = false;       // the layer holds audio that no file has
        Status status = NotBound;
        std::string fileName;       // just the name of the file
        std::string fullPath;       // where that file is now (absolute)
        std::string message;        // a short sentence for the status line ("saved", "missing: ...")
    };

    explicit LayerLibrary (Engine& e);

    void setHomeFolder (const std::string& absolutePath);
    std::string homeFolder() const;

    // Save a layer to a WAV file (absolute path; the folder is created if needed). Asynchronous: finished by tick().
    bool saveLayer (int layer, const std::string& file, std::string& whyNot);
    // Load a WAV file into a layer and bind the layer to it. The file is read now; the hand-over to the audio thread finishes in tick().
    bool loadLayer (int layer, const std::string& file, std::string& whyNot);
    // Load several files at once (a Scene). Every file is read and checked first: if any one fails, nothing is loaded and 'whyNot' says which.
    // Layers not listed are left alone.
    bool loadMany (const std::vector<std::pair<int, std::string>>& files, std::string& whyNot);

    // Save a Scene: every layer that holds audio goes to <folder>/<n>_<name>.wav, plus the settings text as "Ceo scene.txt".
    // Nothing is deleted: files already in the folder are renamed to "_was_<date>_<name>" once all the audio has been collected, and the new files
    // are written under temporary names first, so a failure leaves the folder as it was. Finished by tick(); the result comes from takeSceneSaved().
    bool saveScene (const std::string& folder, const std::string& settingsText, std::string& whyNot);
    struct SceneSaved { bool ok = false; std::string folder; std::string message; std::vector<int> layers; };
    bool takeSceneSaved (SceneSaved& out);      // true once, when a Scene save has finished (or failed)

    // Forget the file of a layer (the audio stays).
    void unbind (int layer);

    // What a preset / project stores, and bringing it back. restore() loads each listed layer when the engine is ready (and keeps a layer that
    // is already bound to the same file and still holds its audio). Layers not listed keep their audio but lose any binding.
    std::vector<std::pair<int, Binding>> bindings() const;
    void restore (const std::vector<std::pair<int, Binding>>& list);

    void tick();                       // about 10 times a second
    LayerInfo info (int layer) const;
    bool busy() const;
    std::string suggestedFileName (int layer) const;   // "Ceo beta 2026-11-04 0712.wav"

    // helpers (public for tests)
    static uint64_t hashBytes (const uint8_t* data, size_t n);
    static std::string normalise (const std::string& p);

private:
    struct Slot
    {
        bool bound = false;
        Binding b;
        uint32_t rev = 0, edit = 0;
        Status status = NotBound;
        std::string message;
        bool queued = false;            // a job for this layer is waiting or running
    };
    struct Job
    {
        enum Type { Save, Load } type = Load;
        int layer = 0;
        std::string path;               // Save: destination.  Load: the file (absolute), or empty for a restore (resolved from the binding)
        bool restore = false;
        Binding want;                   // restore: what to find
        int stage = 0;
        int attempts = 0;               // how often the engine turned an import down
        int waitTicks = 0;              // how long we have been waiting for the audio thread
        LayerAudio audio;
        std::string chosen;             // the file actually used
        uint64_t hash = 0, size = 0;
        bool changed = false;
    };

    struct SceneJob
    {
        bool active = false;
        std::string folder, settingsText;
        std::vector<int> layers;                    // the layers being saved
        std::vector<LayerAudio> audio;              // collected so far
        std::vector<uint32_t> rev, edit;
        size_t next = 0;
        int stage = 0, attempts = 0;
    };
    void stepScene();
    void finishScene (bool ok, const std::string& message);

    Engine& engine;
    SceneJob scene;
    SceneSaved sceneDone; bool sceneDoneReady = false;
    mutable std::mutex m;
    std::string home;
    Slot slots[kNumLayers];
    std::deque<Job> queue;

    std::string toStored (const std::string& absolute) const;       // absolute -> relative-to-home if inside it
    std::string toAbsolute (const std::string& stored) const;
    bool findFile (const Binding& want, std::string& chosen, uint64_t& hash, uint64_t& size, bool& changed) const;
    static bool readFile (const std::string& path, std::vector<uint8_t>& out);
    void bindSlot (int layer, const std::string& absoluteFile, uint64_t hash, uint64_t size, uint32_t rev, uint32_t edit, Status st, const std::string& msg);
    void enqueue (Job j);
    bool stepJob (Job& j);              // true when finished
};

} // namespace ceo
