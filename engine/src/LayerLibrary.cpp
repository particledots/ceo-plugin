#include "ceo/LayerLibrary.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace ceo
{

namespace
{
    std::string lowerExt (const fs::path& p)
    {
        std::string e = p.extension().string();
        std::transform (e.begin(), e.end(), e.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
        return e;
    }
    std::string nameOf (const std::string& p) { return fs::path (p).filename().string(); }

    const char* const kLayerNames[kNumLayers] = { "alpha", "beta", "gamma", "delta", "epsilon", "zeta" };
}

LayerLibrary::LayerLibrary (Engine& e) : engine (e) {}

uint64_t LayerLibrary::hashBytes (const uint8_t* d, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) { h ^= d[i]; h *= 1099511628211ull; }
    return h;
}

std::string LayerLibrary::normalise (const std::string& p)
{
    std::string s = p;
    std::replace (s.begin(), s.end(), '\\', '/');
    return s;
}

void LayerLibrary::setHomeFolder (const std::string& p) { std::lock_guard<std::mutex> g (m); home = p; }
std::string LayerLibrary::homeFolder() const { std::lock_guard<std::mutex> g (m); return home; }

std::string LayerLibrary::toStored (const std::string& absolute) const
{
    if (! home.empty())
    {
        std::error_code ec;
        const fs::path rel = fs::path (absolute).lexically_relative (fs::path (home));
        const std::string r = rel.generic_string();
        if (! ec && ! r.empty() && r.rfind ("..", 0) != 0 && ! rel.is_absolute()) return r;
    }
    return normalise (absolute);
}

std::string LayerLibrary::toAbsolute (const std::string& stored) const
{
    fs::path p (stored);
    if (p.is_absolute()) return p.string();
    if (home.empty()) return {};
    return (fs::path (home) / p).string();
}

bool LayerLibrary::readFile (const std::string& path, std::vector<uint8_t>& out)
{
    std::ifstream f (path, std::ios::binary);
    if (! f) return false;
    f.seekg (0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n < 0 || n > 300ll * 1024 * 1024) return false;
    f.seekg (0);
    out.resize ((size_t) n);
    if (n > 0) f.read (reinterpret_cast<char*> (out.data()), n);
    return (bool) f || f.eof();
}

// 1. the stored path, if it holds the same file; 2. any file under the home folder with the same size and fingerprint;
// 3. the stored path, if there is a file there that has changed; otherwise not found.
bool LayerLibrary::findFile (const Binding& want, std::string& chosen, uint64_t& hash, uint64_t& size, bool& changed) const
{
    changed = false;
    std::error_code ec;
    std::vector<uint8_t> bytes;
    const std::string stored = toAbsolute (want.path);
    bool storedExists = ! stored.empty() && fs::is_regular_file (stored, ec);
    if (storedExists && readFile (stored, bytes))
    {
        if (bytes.size() == want.size && hashBytes (bytes.data(), bytes.size()) == want.hash)
        { chosen = stored; hash = want.hash; size = want.size; return true; }
    }
    if (! home.empty() && fs::is_directory (home, ec))
    {
        size_t visited = 0;
        for (fs::recursive_directory_iterator it (home, fs::directory_options::skip_permission_denied, ec), end; ! ec && it != end; it.increment (ec))
        {
            if (++visited > 50000) break;
            std::error_code e2;
            if (! it->is_regular_file (e2) || lowerExt (it->path()) != ".wav") continue;
            if (it->file_size (e2) != want.size || e2) continue;
            if (readFile (it->path().string(), bytes) && hashBytes (bytes.data(), bytes.size()) == want.hash)
            { chosen = it->path().string(); hash = want.hash; size = want.size; return true; }
        }
    }
    if (storedExists && readFile (stored, bytes))
    { chosen = stored; hash = hashBytes (bytes.data(), bytes.size()); size = bytes.size(); changed = true; return true; }
    return false;
}

void LayerLibrary::bindSlot (int layer, const std::string& absFile, uint64_t hash, uint64_t size, uint32_t rev, uint32_t edit, Status st, const std::string& msg)
{
    Slot& s = slots[layer];
    s.bound = true;
    s.b.path = toStored (absFile); s.b.hash = hash; s.b.size = size;
    s.rev = rev; s.edit = edit;
    s.status = st; s.message = msg;
}

void LayerLibrary::enqueue (Job j)
{
    slots[j.layer].queued = true;
    queue.push_back (std::move (j));
}

bool LayerLibrary::saveLayer (int layer, const std::string& file, std::string& whyNot)
{
    std::lock_guard<std::mutex> g (m);
    if (layer < 0 || layer >= kNumLayers) { whyNot = "Pick a layer (alpha to zeta) first."; return false; }
    if (slots[layer].queued) { whyNot = "That layer is busy - try again in a moment."; return false; }
    if (engine.layerState (layer) == Engine::LayerRecording) { whyNot = "That layer is recording - stop it first (Capture), then save."; return false; }
    if (engine.layerState (layer) != Engine::LayerFilled) { whyNot = "That layer is empty - nothing to save."; return false; }
    Job j; j.type = Job::Save; j.layer = layer; j.path = file;
    slots[layer].status = Working; slots[layer].message = "saving...";
    enqueue (std::move (j));
    return true;
}

bool LayerLibrary::loadLayer (int layer, const std::string& file, std::string& whyNot)
{
    std::lock_guard<std::mutex> g (m);
    if (layer < 0 || layer >= kNumLayers) { whyNot = "Pick a layer (alpha to zeta) first."; return false; }
    if (slots[layer].queued) { whyNot = "That layer is busy - try again in a moment."; return false; }
    if (engine.anyLayerRecording()) { whyNot = "A recording is running - stop it first (Capture), then load."; return false; }
    std::vector<uint8_t> bytes;
    if (! readFile (file, bytes)) { whyNot = "Couldn't read that file (or it is over 300 MB)."; return false; }
    Job j; j.type = Job::Load; j.layer = layer; j.path = file;
    if (! readWav (bytes.data(), bytes.size(), j.audio, whyNot)) return false;
    j.hash = hashBytes (bytes.data(), bytes.size()); j.size = bytes.size(); j.chosen = file; j.stage = 1;
    slots[layer].status = Working; slots[layer].message = "loading...";
    enqueue (std::move (j));
    return true;
}

bool LayerLibrary::loadMany (const std::vector<std::pair<int, std::string>>& files, std::string& whyNot)
{
    std::lock_guard<std::mutex> g (m);
    if (files.empty()) { whyNot = "There are no files to load."; return false; }
    if (scene.active) { whyNot = "A Scene is being saved - try again in a moment."; return false; }
    if (engine.anyLayerRecording()) { whyNot = "A recording is running - stop it first (Capture), then load."; return false; }
    std::vector<Job> jobs;
    bool seen[kNumLayers] = {};
    for (const auto& [layer, file] : files)
    {
        if (layer < 0 || layer >= kNumLayers || seen[layer]) { whyNot = "Internal error: bad layer list."; return false; }
        seen[layer] = true;
        if (slots[layer].queued) { whyNot = "Layer " + std::string (kLayerNames[layer]) + " is busy - try again in a moment."; return false; }
        std::vector<uint8_t> bytes;
        if (! readFile (file, bytes)) { whyNot = "Couldn't read " + nameOf (file) + " (or it is over 300 MB). Nothing was loaded."; return false; }
        Job j; j.type = Job::Load; j.layer = layer; j.path = file;
        std::string err;
        if (! readWav (bytes.data(), bytes.size(), j.audio, err)) { whyNot = nameOf (file) + ": " + err + " Nothing was loaded."; return false; }
        j.hash = hashBytes (bytes.data(), bytes.size()); j.size = bytes.size(); j.chosen = file; j.stage = 1;
        jobs.push_back (std::move (j));
    }
    for (auto& j : jobs)
    {
        slots[j.layer].status = Working; slots[j.layer].message = "loading...";
        enqueue (std::move (j));
    }
    return true;
}

bool LayerLibrary::saveScene (const std::string& folder, const std::string& settingsText, std::string& whyNot)
{
    std::lock_guard<std::mutex> g (m);
    if (folder.empty()) { whyNot = "Pick a folder first."; return false; }
    if (scene.active || ! queue.empty()) { whyNot = "Ceo is still busy with a save or load - try again in a moment."; return false; }
    if (engine.anyLayerRecording()) { whyNot = "A recording is running - stop it first (Capture), then save the Scene."; return false; }
    SceneJob j;
    for (int i = 0; i < kNumLayers; ++i) if (engine.layerState (i) == Engine::LayerFilled) j.layers.push_back (i);
    if (j.layers.empty()) { whyNot = "No layer holds audio - there is nothing to save."; return false; }
    j.active = true; j.folder = folder; j.settingsText = settingsText;
    for (int i : j.layers) { slots[i].queued = true; slots[i].status = Working; slots[i].message = "saving scene..."; }
    scene = std::move (j);
    return true;
}

bool LayerLibrary::takeSceneSaved (SceneSaved& out)
{
    std::lock_guard<std::mutex> g (m);
    if (! sceneDoneReady) return false;
    out = sceneDone; sceneDoneReady = false;
    return true;
}

void LayerLibrary::finishScene (bool ok, const std::string& message)
{
    for (int i : scene.layers) slots[i].queued = false;
    sceneDone = SceneSaved(); sceneDone.ok = ok; sceneDone.folder = scene.folder; sceneDone.message = message;
    if (ok) sceneDone.layers = scene.layers;
    sceneDoneReady = true;
    scene = SceneJob();
}

void LayerLibrary::stepScene()
{
    SceneJob& j = scene;
    // 1. collect the audio, one layer after another (the engine has a single export slot)
    if (j.next < j.layers.size())
    {
        const int layer = j.layers[j.next];
        if (j.stage == 0)
        {
            if (engine.requestExport (layer)) { j.stage = 1; j.attempts = 0; }
            else if (++j.attempts > 3000) { for (int i : j.layers) { slots[i].status = Failed; slots[i].message = "scene not saved"; } finishScene (false, "Ceo couldn't get the audio out (is Logic playing?). Nothing was changed."); }
            return;
        }
        LayerAudio a;
        const auto r = engine.pollExport (a);
        if (r == Engine::IoNone)
        {
            if (++j.attempts > 3000) { for (int i : j.layers) { slots[i].status = Failed; slots[i].message = "scene not saved"; } finishScene (false, "The audio never answered (press Play if Logic is stopped). Nothing was changed."); }
            else if (j.attempts == 30) slots[layer].message = "waiting for audio to run (press Play if Logic is stopped)...";
            return;
        }
        if (r == Engine::ExportEmpty) { for (int i : j.layers) { slots[i].status = slots[i].bound ? Ok : NotBound; slots[i].message.clear(); } finishScene (false, "A layer was emptied while saving. Nothing was changed."); return; }
        j.audio.push_back (std::move (a));
        j.rev.push_back (engine.exportedRevision()); j.edit.push_back (engine.exportedEditCount());
        ++j.next; j.stage = 0; j.attempts = 0;
        return;
    }

    // 2. everything is in memory: write the new files under temporary names first
    std::error_code ec;
    const fs::path dir (j.folder);
    fs::create_directories (dir, ec);
    auto cleanup = [&] (const std::vector<fs::path>& tmps) { std::error_code e; for (const auto& t : tmps) fs::remove (t, e); };
    std::vector<fs::path> tmps, finals;
    std::vector<std::vector<uint8_t>> wavs;
    bool ok = fs::is_directory (dir, ec);
    for (size_t k = 0; k < j.layers.size() && ok; ++k)
    {
        const std::string name = std::to_string (j.layers[k] + 1) + "_" + kLayerNames[j.layers[k]] + ".wav";
        wavs.push_back (writeWav16 (j.audio[k]));
        const fs::path tmp = dir / (name + ".ceo-tmp");
        std::ofstream f (tmp, std::ios::binary | std::ios::trunc);
        if (f) f.write (reinterpret_cast<const char*> (wavs.back().data()), (std::streamsize) wavs.back().size());
        const bool good = f && f.good();
        f.close();
        tmps.push_back (tmp); finals.push_back (dir / name);
        ok = good;
    }
    if (ok)
    {
        const fs::path tmp = dir / "Ceo scene.txt.ceo-tmp";
        std::ofstream f (tmp, std::ios::binary | std::ios::trunc);
        if (f) f << j.settingsText;
        ok = f && f.good();
        f.close();
        tmps.push_back (tmp); finals.push_back (dir / "Ceo scene.txt");
    }
    if (! ok)
    {
        cleanup (tmps);
        for (int i : j.layers) { slots[i].status = Failed; slots[i].message = "scene not saved"; }
        finishScene (false, "Couldn't write into " + nameOf (j.folder) + " (is the folder writable?). Nothing was changed.");
        return;
    }

    // 3. rename what was there (audio files, and an older Ceo scene.txt); Arbhar's own .txt stays untouched
    const std::time_t t = std::time (nullptr);
    std::tm tmv {};
#ifdef _WIN32
    localtime_s (&tmv, &t);
#else
    localtime_r (&t, &tmv);
#endif
    char stamp[32];
    std::snprintf (stamp, sizeof stamp, "%04d%02d%02d-%02d%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
    int renamed = 0;
    std::vector<fs::path> olds;
    for (fs::directory_iterator it (dir, fs::directory_options::skip_permission_denied, ec), end; ! ec && it != end; it.increment (ec))
    {
        std::error_code e2;
        if (! it->is_regular_file (e2)) continue;
        const std::string name = it->path().filename().string();
        if (name.empty() || name[0] == '.' || name.rfind ("_was_", 0) == 0) continue;
        if (lowerExt (it->path()) == ".wav" || name == "Ceo scene.txt") olds.push_back (it->path());
    }
    for (const auto& o : olds)
    {
        const std::string base = o.filename().string();
        fs::path target;
        for (int n = 0; n < 1000; ++n)
        {
            target = dir / (std::string ("_was_") + stamp + (n ? "_" + std::to_string (n + 1) : std::string()) + "_" + base);
            std::error_code e3;
            if (! fs::exists (target, e3)) break;
        }
        std::error_code e4;
        fs::rename (o, target, e4);
        if (e4) { cleanup (tmps); for (int i : j.layers) { slots[i].status = Failed; slots[i].message = "scene not saved"; }
                  finishScene (false, "Couldn't rename the existing file " + base + ". The new files were not written."); return; }
        ++renamed;
    }

    // 4. the new files take their real names
    for (size_t k = 0; k < tmps.size(); ++k)
    {
        std::error_code e5;
        fs::rename (tmps[k], finals[k], e5);
        if (e5) { cleanup (tmps); for (int i : j.layers) { slots[i].status = Failed; slots[i].message = "scene not saved"; }
                  finishScene (false, "Couldn't finish writing " + finals[k].filename().string() + ". Your older files were renamed to _was_... in the same folder."); return; }
    }
    for (size_t k = 0; k < j.layers.size(); ++k)
        bindSlot (j.layers[k], finals[k].string(), hashBytes (wavs[k].data(), wavs[k].size()), wavs[k].size(), j.rev[k], j.edit[k], Ok, "saved " + finals[k].filename().string());
    finishScene (true, "Scene saved: " + std::to_string (j.layers.size()) + " WAV files and Ceo scene.txt in " + nameOf (j.folder)
                         + (renamed ? " (" + std::to_string (renamed) + " older file" + (renamed == 1 ? "" : "s") + " renamed to _was_...)" : ""));
}

void LayerLibrary::unbind (int layer)
{
    std::lock_guard<std::mutex> g (m);
    if (layer < 0 || layer >= kNumLayers) return;
    Slot& s = slots[layer];
    s.bound = false; s.status = NotBound; s.message.clear(); s.b = Binding();
    // a queued restore for this layer is dropped
    for (auto it = queue.begin(); it != queue.end(); ++it)
        if (it->layer == layer && it->restore && it->stage == 0) { queue.erase (it); s.queued = false; break; }
}

std::vector<std::pair<int, LayerLibrary::Binding>> LayerLibrary::bindings() const
{
    std::lock_guard<std::mutex> g (m);
    std::vector<std::pair<int, Binding>> out;
    for (int i = 0; i < kNumLayers; ++i) if (slots[i].bound) out.push_back ({ i, slots[i].b });
    return out;
}

void LayerLibrary::restore (const std::vector<std::pair<int, Binding>>& list)
{
    std::lock_guard<std::mutex> g (m);
    bool listed[kNumLayers] = {};
    for (const auto& [layer, b] : list)
    {
        if (layer < 0 || layer >= kNumLayers) continue;
        listed[layer] = true;
        Slot& s = slots[layer];
        const bool holds = engine.layerState (layer) == Engine::LayerFilled;
        if (s.bound && s.b.hash == b.hash && s.b.size == b.size && holds && ! s.queued)
        {   // already there (the host set the same state again): keep the audio, take the (possibly updated) path
            s.b.path = b.path; continue;
        }
        // a queued job for this layer is replaced
        for (auto it = queue.begin(); it != queue.end(); ++it)
            if (it->layer == layer && it->stage <= 1) { queue.erase (it); break; }
        s.queued = false;
        s.bound = true; s.b = b; s.status = Waiting; s.message = "waiting to load " + nameOf (b.path);
        s.rev = engine.layerRevision (layer); s.edit = engine.layerEditCount (layer);
        Job j; j.type = Job::Load; j.layer = layer; j.restore = true; j.want = b; j.stage = 0;
        enqueue (std::move (j));
    }
    for (int i = 0; i < kNumLayers; ++i)
        if (! listed[i] && slots[i].bound)
        {
            slots[i].bound = false; slots[i].status = NotBound; slots[i].message.clear();
            for (auto it = queue.begin(); it != queue.end(); ++it)
                if (it->layer == i && it->restore && it->stage == 0) { queue.erase (it); slots[i].queued = false; break; }
        }
}

LayerLibrary::LayerInfo LayerLibrary::info (int layer) const
{
    std::lock_guard<std::mutex> g (m);
    LayerInfo r;
    if (layer < 0 || layer >= kNumLayers) return r;
    const Slot& s = slots[layer];
    r.bound = s.bound; r.status = s.status; r.message = s.message;
    r.fileName = s.bound ? nameOf (s.b.path) : std::string();
    r.fullPath = s.bound ? toAbsolute (s.b.path) : std::string();
    r.unsaved = ! s.bound && engine.layerState (layer) == Engine::LayerFilled;
    return r;
}

bool LayerLibrary::busy() const { std::lock_guard<std::mutex> g (m); return ! queue.empty() || scene.active; }

std::string LayerLibrary::suggestedFileName (int layer) const
{
    const std::time_t t = std::time (nullptr);
    std::tm tmv {};
#ifdef _WIN32
    localtime_s (&tmv, &t);
#else
    localtime_r (&t, &tmv);
#endif
    char buf[96];
    std::snprintf (buf, sizeof buf, "Ceo %s %04d-%02d-%02d %02d%02d.wav", kLayerNames[std::max (0, std::min (kNumLayers - 1, layer))],
                   tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
    return buf;
}

// One step of the job at the front of the queue. Returns true when it is finished (done, failed or given up).
bool LayerLibrary::stepJob (Job& j)
{
    Slot& s = slots[j.layer];
    if (j.type == Job::Save)
    {
        if (j.stage == 0)
        {
            if (! engine.requestExport (j.layer)) return false;    // try again next tick
            j.stage = 1; return false;
        }
        LayerAudio a;
        const auto r = engine.pollExport (a);
        if (r == Engine::IoNone)
        {
            if (++j.attempts == 30) s.message = "waiting for audio to run (press Play if Logic is stopped)...";
            return false;
        }
        if (r == Engine::ExportEmpty) { s.status = s.bound ? s.status : NotBound; s.message = "that layer is empty - nothing saved"; return true; }
        const auto bytes = writeWav16 (a);
        std::error_code ec;
        fs::path dest (j.path);
        if (dest.has_parent_path()) fs::create_directories (dest.parent_path(), ec);
        std::ofstream f (j.path, std::ios::binary | std::ios::trunc);
        if (f) f.write (reinterpret_cast<const char*> (bytes.data()), (std::streamsize) bytes.size());
        if (! f || ! f.good()) { s.message = "couldn't write " + nameOf (j.path); s.status = Failed; return true; }
        f.close();
        bindSlot (j.layer, j.path, hashBytes (bytes.data(), bytes.size()), bytes.size(), engine.exportedRevision(), engine.exportedEditCount(), Ok, "saved " + nameOf (j.path));
        return true;
    }

    // Load (a file chosen by the user, or a binding being restored)
    if (j.stage == 0)
    {
        j.attempts = 0;
        bool changed = false;
        if (! findFile (j.want, j.chosen, j.hash, j.size, changed))
        {
            s.status = Missing; s.message = "missing: " + nameOf (j.want.path) + (home.empty() ? " (no home folder set)" : "");
            return true;
        }
        std::vector<uint8_t> bytes; std::string err;
        if (! readFile (j.chosen, bytes) || ! readWav (bytes.data(), bytes.size(), j.audio, err))
        { s.status = Failed; s.message = "couldn't read " + nameOf (j.chosen) + (err.empty() ? "" : ": " + err); return true; }
        j.changed = changed; j.stage = 1;
        s.status = Working; s.message = "loading " + nameOf (j.chosen) + "...";
    }
    if (j.stage == 1)
    {
        std::string why;
        if (engine.submitImport (j.layer, j.audio, why)) { j.stage = 2; return false; }
        const bool retry = ! engine.layerFormat().valid || why.rfind ("Still busy", 0) == 0;
        if (retry) { s.status = Waiting; s.message = "waiting to load " + nameOf (j.chosen); return false; }
        s.status = Failed; s.message = why; return true;
    }
    const auto r = engine.pollImport();
    if (r == Engine::IoNone)
    {
        if (++j.waitTicks == 30) s.message = "waiting for audio to run (press Play if Logic is stopped)...";
        return false;
    }
    if (r == Engine::ImportRejected)
    {   // the format changed or a recording was running meanwhile: try again
        if (++j.attempts > 200) { s.status = Failed; s.message = "couldn't load " + nameOf (j.chosen); return true; }
        j.stage = 1; j.waitTicks = 0; return false;
    }
    bindSlot (j.layer, j.chosen, j.hash, j.size, engine.layerRevision (j.layer), engine.layerEditCount (j.layer),
              j.changed ? Changed : Ok, j.changed ? "loaded " + nameOf (j.chosen) + " (the file has changed since it was saved)" : "loaded " + nameOf (j.chosen));
    return true;
}

void LayerLibrary::tick()
{
    std::lock_guard<std::mutex> g (m);

    // 1. the job at the front
    if (! queue.empty())
    {
        Job& j = queue.front();
        if (stepJob (j))
        {
            slots[j.layer].queued = false;
            queue.pop_front();
        }
    }

    if (scene.active) stepScene();

    // 2. bound layers whose audio changed under them
    for (int i = 0; i < kNumLayers; ++i)
    {
        Slot& s = slots[i];
        if (! s.bound || s.queued) continue;
        const uint32_t rev = engine.layerRevision (i), edit = engine.layerEditCount (i);
        if (rev == s.rev && edit == s.edit) continue;
        if (edit != s.edit)
        {   // the user recorded over it or cleared it: it no longer matches its file
            s.bound = false; s.status = NotBound; s.message.clear();
        }
        else if (engine.layerState (i) == Engine::LayerEmpty)
        {   // emptied by a format change: load it again from its file in the new format
            s.status = Waiting; s.message = "reloading " + nameOf (s.b.path);
            Job j; j.type = Job::Load; j.layer = i; j.restore = true; j.want = s.b; j.stage = 0;
            s.rev = rev;
            enqueue (std::move (j));
        }
        else s.rev = rev;
    }
}

} // namespace ceo
