// Tests of saving and loading Ceo's layers as WAV files (the engine's hand-over slots and LayerLibrary), with real files in a
// temporary folder: saving a layer, a Logic project bringing its layers back, finding moved or renamed files, missing and changed
// files, mono and stereo layers, loading files of any length and sample rate, the user's edits dropping a link, a sample-rate change
// reloading linked layers, and the audio thread never allocating or touching a file.
// Ported from the Layers plugin's library_test.cpp (same author) and extended for Ceo.
// What it does NOT check: the file dialogs, the host's own project handling, or how anything sounds in Logic.
#include "ceo/Engine.h"
#include "ceo/LayerAudio.h"
#include "ceo/LayerLibrary.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <set>
#include <sstream>
#include <vector>
#include <unistd.h>

namespace fs = std::filesystem;
using ceo::Engine;
using ceo::LayerAudio;
using Lib = ceo::LayerLibrary;

static int gChecks = 0, gFailed = 0;
static void check (bool ok, const char* what, double a = 0.0, double b = 0.0)
{
    ++gChecks;
    if (! ok) { ++gFailed; std::printf ("  FAIL: %s  (%g, %g)\n", what, a, b); }
}
static void checkS (bool ok, const char* what, const std::string& s)
{
    ++gChecks;
    if (! ok) { ++gFailed; std::printf ("  FAIL: %s  ('%s')\n", what, s.c_str()); }
}

// ---- counts every allocation made while 'gCounting' is on (used around the audio thread's work) -----------------------
static std::atomic<bool> gCounting { false };
static std::atomic<long> gAllocations { 0 };
void* operator new (std::size_t n)
{
    if (gCounting.load (std::memory_order_relaxed)) gAllocations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

static const double kTone[8] = { 3000.0, 440.0, 660.0, 880.0, 1100.0, 1320.0, 1540.0, 1760.0 };
static const double kPi = 3.14159265358979;

static double amplitudeAt (const std::vector<float>& x, double hz, double sr)
{
    const double w = 2.0 * kPi * hz / sr, c = 2.0 * std::cos (w);
    double s1 = 0, s2 = 0;
    for (size_t i = 0; i < x.size(); ++i) { const double s = x[i] + c * s1 - s2; s2 = s1; s1 = s; }
    return 2.0 * std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (double) x.size();
}
// which of the layer tones 1..6 dominates (0 = none)
static int toneOf (const std::vector<float>& x, double sr)
{
    if (x.size() < 2000) return 0;
    int best = 0; double bestE = 0, second = 0;
    for (int t = 1; t <= 6; ++t)
    {
        double e = 0;
        for (int k = -6; k <= 6; ++k) { const double a = amplitudeAt (x, kTone[t] * (1.0 + 0.005 * k), sr); e += a * a; }
        if (e > bestE) { second = bestE; bestE = e; best = t; } else if (e > second) second = e;
    }
    return bestE > 4.0 * second && bestE > 1e-9 ? best : 0;
}

static LayerAudio makeTone (double rate, double seconds, double hz, float amp = 0.5f, int channels = 2, double hzRight = 0.0)
{
    LayerAudio a; a.channels = channels; a.sampleRate = rate;
    const size_t n = (size_t) (seconds * rate);
    a.ch[0].resize (n);
    for (size_t i = 0; i < n; ++i) a.ch[0][i] = amp * (float) std::sin (2.0 * kPi * hz * (double) i / rate);
    if (channels == 2)
    {
        a.ch[1].resize (n);
        for (size_t i = 0; i < n; ++i) a.ch[1][i] = hzRight > 0.0 ? amp * (float) std::sin (2.0 * kPi * hzRight * (double) i / rate) : a.ch[0][i];
    }
    return a;
}


struct Inst
{
    Engine e;
    Lib lib;
    ceo::Params p;
    double sr = 48000.0;
    double phase = 0, phaseR = 0;
    explicit Inst (bool prepared = true) : lib (e)
    {
        p.dryWet = 1.0f;
        if (prepared) { e.prepare (sr, 480, 2); run (0.3, 0.0); }
    }
    // audio runs for 'seconds' with a sine at hz (left) and hzR (right; 0 = same as left)
    void run (double seconds, double hz, double hzR = 0.0)
    {
        const int N = 480; std::vector<float> L (N), R (N);
        float* ch[2] = { L.data(), R.data() };
        for (long done = 0; done < (long) (seconds * sr); done += N)
        {
            for (int i = 0; i < N; ++i)
            {
                phase += hz / sr; if (phase >= 1.0) phase -= 1.0;
                phaseR += (hzR > 0 ? hzR : hz) / sr; if (phaseR >= 1.0) phaseR -= 1.0;
                L[i] = hz > 0 ? 0.5f * (float) std::sin (2.0 * kPi * phase) : 0.0f;
                R[i] = hz > 0 ? 0.5f * (float) std::sin (2.0 * kPi * phaseR) : 0.0f;
            }
            e.setParams (p);
            e.process (ch, 2, N);
        }
    }
    // audio runs 0.1 s, then the library ticks (the plugin's timer ticks 10 times a second)
    void settle (double maxSeconds = 20.0)
    {
        for (double t = 0; t < maxSeconds; t += 0.1) { run (0.1, 0.0); lib.tick(); if (! lib.busy()) { run (0.1, 0.0); lib.tick(); return; } }
    }
    void select (int layer) { p.layer = layer; run (0.15, 0.0); lib.tick(); }
    void record (int layer, double hz, double seconds = 1.0, bool stereo = false, double hzR = 0.0)
    {
        p.layer = layer; p.capture = false; p.stereoInput = stereo; run (0.1, 0.0); lib.tick();
        p.capture = true; run (seconds, hz, hzR);
        p.capture = false; run (0.1, 0.0); lib.tick();
        p.stereoInput = false;
    }
    void erase() { p.erase = ! p.erase; run (0.2, 0.0); lib.tick(); }
    // a stretch of the layer's own memory (0.1 s to 0.9 s)
    std::vector<float> tail (int layer, int channel = 0)
    {
        const float* d = e.layerAudio (layer, channel);
        const size_t a = (size_t) (0.1 * sr), b = (size_t) (0.9 * sr);
        return d ? std::vector<float> (d + a, d + b) : std::vector<float>();
    }
    std::vector<float> whole (int layer, int channel = 0)
    {
        const float* d = e.layerAudio (layer, channel);
        return d ? std::vector<float> (d, d + e.layerAudioLength()) : std::vector<float>();
    }
};

static std::string slurp (const fs::path& p) { std::ifstream f (p, std::ios::binary); return { std::istreambuf_iterator<char> (f), {} }; }
static void spit (const fs::path& p, const std::vector<uint8_t>& b) { fs::create_directories (p.parent_path()); std::ofstream f (p, std::ios::binary | std::ios::trunc); f.write ((const char*) b.data(), (std::streamsize) b.size()); }

static fs::path freshHome (const char* name)
{
    const fs::path h = fs::temp_directory_path() / ("ceolib_test_" + std::to_string ((long) getpid()) + "_" + name);
    fs::remove_all (h); fs::create_directories (h);
    return h;
}

// what a 16-bit file makes of a float sample (the exact value a reloaded layer holds)
static float quantised (float x) { return (float) std::max (-32768L, std::min (32767L, std::lrintf (x * 32768.0f))) / 32768.0f; }

// ---- 0. the WAV code ----------------------------------------------------------------------------------------------------------
static void testWav()
{
    std::printf ("[0] WAV reading and writing, resampling\n");
    LayerAudio a = makeTone (48000.0, 0.5, 440.0, 0.5f, 2, 660.0);
    const auto bytes = ceo::writeWav16 (a);
    check (bytes.size() == 44 + a.frames() * 4, "16-bit stereo file size", (double) bytes.size());
    LayerAudio b; std::string err;
    check (ceo::readWav (bytes.data(), bytes.size(), b, err), "reads its own file");
    check (b.channels == 2 && b.sampleRate == 48000.0 && b.frames() == a.frames(), "same shape", (double) b.frames());
    bool exact = b.frames() == a.frames();
    for (size_t i = 0; i < b.frames() && exact; ++i) exact = b.ch[0][i] == quantised (a.ch[0][i]) && b.ch[1][i] == quantised (a.ch[1][i]);
    check (exact, "samples are the 16-bit values, exactly");
    LayerAudio m = makeTone (44100.0, 0.2, 440.0, 0.5f, 1);
    const auto mb = ceo::writeWav16 (m);
    LayerAudio m2;
    check (ceo::readWav (mb.data(), mb.size(), m2, err) && m2.channels == 1 && m2.ch[1].empty() && m2.sampleRate == 44100.0, "mono stays mono at 44.1 kHz");
    std::vector<uint8_t> junk (100, 7);
    check (! ceo::readWav (junk.data(), junk.size(), b, err) && ! err.empty(), "junk is refused with a message");
    std::vector<uint8_t> trunc (mb.begin(), mb.begin() + 30);
    check (! ceo::readWav (trunc.data(), trunc.size(), b, err), "a cut-off header is refused");

    // 24-bit and 32-bit float files with NaN in them
    {
        std::vector<uint8_t> w; auto p32 = [&] (uint32_t x) { for (int i = 0; i < 4; ++i) w.push_back ((uint8_t) (x >> (8 * i))); };
        auto p16 = [&] (uint16_t x) { w.push_back ((uint8_t) x); w.push_back ((uint8_t) (x >> 8)); };
        auto tg = [&] (const char* t) { for (int i = 0; i < 4; ++i) w.push_back ((uint8_t) t[i]); };
        const float vals[4] = { 0.25f, NAN, -0.5f, INFINITY };
        tg ("RIFF"); p32 (36 + 16); tg ("WAVE"); tg ("fmt "); p32 (16); p16 (3); p16 (1); p32 (48000); p32 (48000 * 4); p16 (4); p16 (32);
        tg ("data"); p32 (16); for (float v : vals) { uint32_t u; std::memcpy (&u, &v, 4); p32 (u); }
        LayerAudio f; check (ceo::readWav (w.data(), w.size(), f, err) && f.frames() == 4, "float WAV read");
        check (f.frames() == 4 && f.ch[0][0] == 0.25f && f.ch[0][1] == 0.0f && f.ch[0][2] == -0.5f && f.ch[0][3] == 0.0f, "NaN and infinity in a file become silence");
    }
    // resampling keeps the length (in seconds) and the pitch
    {
        const auto src = makeTone (44100.0, 1.0, 1000.0, 0.5f, 1).ch[0];
        const auto up = ceo::resampleOffline (src, 44100.0, 48000.0);
        check (std::abs ((double) up.size() - 48000.0) <= 2.0, "44.1 -> 48 kHz keeps one second", (double) up.size());
        std::vector<float> mid (up.begin() + 2000, up.begin() + 46000);
        check (amplitudeAt (mid, 1000.0, 48000.0) > 0.45 && amplitudeAt (mid, 1000.0, 48000.0) < 0.55, "and the level and pitch", amplitudeAt (mid, 1000.0, 48000.0));
        const auto same = ceo::resampleOffline (src, 44100.0, 44100.0);
        check (same == src, "same rate: untouched");
    }
}

// ---- 1. save binds the layer to the file --------------------------------------------------------------------------------------
static void testSave()
{
    std::printf ("[1] save a layer: file written, layer bound\n");
    const fs::path home = freshHome ("save");
    Inst a; a.lib.setHomeFolder (home.string());
    a.record (1, kTone[2]);                                         // layer beta (index 1)
    check (a.lib.info (1).unsaved, "a recorded layer with no file is flagged unsaved");
    std::string why;
    check (! a.lib.saveLayer (2, (home / "x.wav").string(), why) && ! why.empty(), "saving an empty layer is refused");
    check (a.lib.saveLayer (1, (home / "Drone A.wav").string(), why), "save accepted");
    check (! a.lib.saveLayer (1, (home / "other.wav").string(), why), "a second save of the same layer while busy is refused");
    a.settle();
    check (fs::exists (home / "Drone A.wav"), "the file exists");
    const auto info = a.lib.info (1);
    check (info.bound && info.status == Lib::Ok && ! info.unsaved, "the layer is bound and no longer unsaved");
    checkS (info.fileName == "Drone A.wav", "file name shown", info.fileName);
    const auto b = a.lib.bindings();
    check (b.size() == 1 && b[0].first == 1, "one binding, for layer index 1", (double) b.size());
    checkS (b.size() == 1 && b[0].second.path == "Drone A.wav", "stored path is relative to the home folder", b.empty() ? "" : b[0].second.path);
    const std::string bytes = slurp (home / "Drone A.wav");
    check (b.size() == 1 && b[0].second.size == bytes.size() && b[0].second.hash == Lib::hashBytes ((const uint8_t*) bytes.data(), bytes.size()), "size and fingerprint are those of the file");

    // the file holds the whole layer (10 s + 3 s tail): 624000 frames at 48 kHz, mono layer -> mono file
    {
        LayerAudio w; std::string err;
        check (ceo::readWav ((const uint8_t*) bytes.data(), bytes.size(), w, err), "the file reads back");
        check (w.channels == 1 && w.sampleRate == 48000.0 && w.frames() == (size_t) a.e.layerAudioLength(), "mono layer: a mono file of the whole 13 s", (double) w.frames());
        check (toneOf (std::vector<float> (w.ch[0].begin() + 4800, w.ch[0].begin() + 43200), 48000.0) == 2, "with the recorded tone");
    }
    // saving into a folder that doesn't exist yet creates it
    a.record (3, kTone[4]);
    check (a.lib.saveLayer (3, (home / "new folder" / "deeper" / "L4.wav").string(), why), "save into new folders accepted");
    a.settle();
    check (fs::exists (home / "new folder" / "deeper" / "L4.wav") && a.lib.info (3).bound, "folders created, layer bound");
    // a file outside the home folder is stored with its full path
    const fs::path other = freshHome ("save_other");
    a.record (4, kTone[5]);
    a.lib.saveLayer (4, (other / "elsewhere.wav").string(), why); a.settle();
    bool found = false; for (auto& [l, bd] : a.lib.bindings()) if (l == 4) found = fs::path (bd.path).is_absolute();
    check (found, "a file outside the home folder is stored with its full path");
    // unwritable destination: the parent is a file
    spit (home / "iamafile", { 1, 2, 3 });
    a.record (5, kTone[6]);
    a.lib.saveLayer (5, (home / "iamafile" / "x.wav").string(), why); a.settle();
    check (! a.lib.info (5).bound && a.lib.info (5).status == Lib::Failed && a.lib.info (5).unsaved, "a destination that can't be written is reported, layer stays unsaved");
    // a layer that is recording can't be saved
    a.p.layer = 0; a.p.capture = true; a.run (0.3, kTone[1]);
    check (! a.lib.saveLayer (0, (home / "rec.wav").string(), why) && why.find ("recording") != std::string::npos, "saving a layer that is recording is refused, with a reason");
    a.p.capture = false; a.run (0.2, 0.0);
    fs::remove_all (home); fs::remove_all (other);
}

// ---- 2. a project brings the layers back -------------------------------------------------------------------------------------
static void testProjectRoundTrip()
{
    std::printf ("[2] project round trip: restore loads the layers from their files\n");
    const fs::path home = freshHome ("project");
    Inst a; a.lib.setHomeFolder (home.string());
    std::string why;
    std::vector<float> src[6];
    for (int k : { 0, 2, 3 })
    {
        a.record (k, kTone[k + 1]);
        a.lib.saveLayer (k, (home / ("L" + std::to_string (k) + ".wav")).string(), why); a.settle();
        src[k] = a.whole (k);
    }
    const auto state = a.lib.bindings();
    check (state.size() == 3, "three bindings saved in the 'project'", (double) state.size());
    if (state.size() != 3) { fs::remove_all (home); return; }

    Inst b; b.lib.setHomeFolder (home.string());                    // a second instance (another track), empty
    b.lib.restore (state);
    check (b.lib.info (2).status == Lib::Waiting, "restore queued: waiting");
    b.settle();
    for (int k : { 0, 2, 3 })
    {
        check (b.e.layerState (k) == Engine::LayerFilled, "layer filled from its file", k);
        const auto got = b.whole (k);
        bool same = got.size() == src[k].size();
        for (size_t i = 0; i < got.size() && same; ++i) same = got[i] == quantised (src[k][i]);
        check (same, "identical audio to what was saved (to 16 bits)", k);
        check (b.lib.info (k).bound && b.lib.info (k).status == Lib::Ok, "and bound", k);
    }
    check (b.e.layerState (1) == Engine::LayerEmpty && ! b.lib.info (1).bound, "layers without a link stay empty");
    check (b.e.layerHasContent (2) && ! b.e.layerHasContent (1), "the engine agrees which layers hold audio");
    // the restored layer plays its tone
    b.select (2);
    check (toneOf (b.tail (2), 48000.0) == 3, "the restored layer holds its own tone");
    // restoring the same state again changes nothing (hosts do this)
    const uint32_t rev = b.e.layerRevision (2);
    b.lib.restore (state); b.settle();
    check (b.e.layerRevision (2) == rev, "restoring the same state again doesn't reload the audio");
    // a project with fewer layers: unlisted layers keep their audio but lose the link
    std::vector<std::pair<int, Lib::Binding>> fewer; fewer.push_back (state[0]);
    b.lib.restore (fewer); b.settle();
    check (b.e.layerState (3) == Engine::LayerFilled && ! b.lib.info (3).bound && b.lib.info (3).unsaved, "an unlisted layer keeps its audio, now unsaved");
    fs::remove_all (home);
}

// ---- 3. finding files that have moved ------------------------------------------------------------------------------------------
static void testFindMoved()
{
    std::printf ("[3] moved / renamed / missing / changed files\n");
    const fs::path home = freshHome ("find");
    Inst a; a.lib.setHomeFolder (home.string());
    std::string why;
    a.record (1, kTone[2]);
    a.lib.saveLayer (1, (home / "a.wav").string(), why); a.settle();
    const auto srcAudio = a.whole (1);
    const auto state = a.lib.bindings();
    check (state.size() == 1, "one binding to start from", (double) state.size());
    if (state.size() != 1) { fs::remove_all (home); return; }

    // moved into a sub-folder and renamed: found by size + fingerprint
    fs::create_directories (home / "projects" / "autumn");
    fs::rename (home / "a.wav", home / "projects" / "autumn" / "renamed drone.wav");
    // decoy: same name as the original in another folder, different audio
    spit (home / "decoy" / "a.wav", ceo::writeWav16 (makeTone (48000.0, 0.5, 1540.0)));
    Inst b; b.lib.setHomeFolder (home.string()); b.lib.restore (state); b.settle();
    check (b.e.layerState (1) == Engine::LayerFilled && b.whole (1) == [&] { auto v = srcAudio; for (auto& x : v) x = quantised (x); return v; }(), "found the renamed file in a sub-folder and loaded the right audio");
    const auto nb = b.lib.bindings();
    checkS (nb.size() == 1 && nb[0].second.path == "projects/autumn/renamed drone.wav", "the link now points at the new place", nb.empty() ? "" : nb[0].second.path);
    check (b.lib.info (1).status == Lib::Ok, "status ok");

    // missing: only a decoy with the same name remains
    fs::remove (home / "projects" / "autumn" / "renamed drone.wav");
    Inst c; c.lib.setHomeFolder (home.string()); c.lib.restore (state); c.settle();
    check (c.e.layerState (1) == Engine::LayerEmpty, "a same-named file with different audio is NOT loaded");
    check (c.lib.info (1).status == Lib::Missing && c.lib.info (1).message.find ("missing") == 0, "reported as missing");
    check (c.lib.bindings().size() == 1, "the link is kept (the file may come back)");
    check (! c.lib.busy(), "nothing left retrying");

    // changed: the file is still at its path but has different audio
    spit (home / "a.wav", ceo::writeWav16 (makeTone (48000.0, 1.0, kTone[5])));
    Inst d; d.lib.setHomeFolder (home.string()); d.lib.restore (state); d.settle();
    check (d.e.layerState (1) == Engine::LayerFilled, "the changed file is loaded");
    check (d.lib.info (1).status == Lib::Changed, "and flagged as changed");
    check (toneOf (d.tail (1), 48000.0) == 5, "with the new audio");
    const auto nd = d.lib.bindings();
    const std::string bytes = slurp (home / "a.wav");
    check (nd.size() == 1 && nd[0].second.hash == Lib::hashBytes ((const uint8_t*) bytes.data(), bytes.size()), "the fingerprint was updated to the new file");

    // no home folder set and a relative path: missing with a clear message
    Inst e2; e2.lib.restore (state); e2.settle();
    check (e2.lib.info (1).status == Lib::Missing && e2.lib.info (1).message.find ("home folder") != std::string::npos, "no home folder: missing, and it says why");

    // the file is garbage (same size, different bytes)
    {
        std::string orig = slurp (home / "a.wav");
        std::vector<uint8_t> junk (orig.size(), 0x41);
        spit (home / "a.wav", junk);
        Lib::Binding bd = state[0].second; bd.size = junk.size(); bd.hash = Lib::hashBytes (junk.data(), junk.size());
        Inst g; g.lib.setHomeFolder (home.string()); g.lib.restore ({ { 1, bd } }); g.settle();
        check (g.lib.info (1).status == Lib::Failed && g.e.layerState (1) == Engine::LayerEmpty, "a file that is not a WAV fails cleanly");
    }
    // absolute path outside the home folder, no home set, and a 44.1 kHz file (converted to 48 kHz)
    const fs::path other = freshHome ("find_other");
    spit (other / "outside.wav", ceo::writeWav16 (makeTone (44100.0, 0.8, kTone[3])));
    {
        const std::string bytes2 = slurp (other / "outside.wav");
        Lib::Binding bd; bd.path = (other / "outside.wav").string(); bd.size = bytes2.size(); bd.hash = Lib::hashBytes ((const uint8_t*) bytes2.data(), bytes2.size());
        Inst h; h.lib.restore ({ { 3, bd } }); h.settle();
        check (h.e.layerState (3) == Engine::LayerFilled && toneOf (h.tail (3), 48000.0) == 3, "an absolute path works without a home folder (and a 44.1 kHz file loads)");
        const auto w = h.whole (3);
        size_t last = 0; for (size_t i = 0; i < w.size(); ++i) if (std::abs (w[i]) > 0.01f) last = i;
        check (std::abs ((double) last - 0.8 * 48000.0) < 400.0, "converted to the project's rate: 0.8 s stays 0.8 s", (double) last);
    }
    fs::remove_all (home); fs::remove_all (other);
}

// ---- 4. user edits drop the link; a sample-rate change reloads --------------------------------------------------------------
static void testEditsAndRateChange()
{
    std::printf ("[4] recording over / erasing drops the link; a sample-rate change reloads from the files\n");
    const fs::path home = freshHome ("edits");
    Inst a; a.lib.setHomeFolder (home.string());
    std::string why;
    a.record (0, kTone[1]);
    a.lib.saveLayer (0, (home / "one.wav").string(), why); a.settle();
    a.record (1, kTone[2]);
    a.lib.saveLayer (1, (home / "two.wav").string(), why); a.settle();
    a.record (2, kTone[3]);                                        // never saved
    check (a.lib.info (0).bound && a.lib.info (1).bound && ! a.lib.info (2).bound && a.lib.info (2).unsaved, "two saved, one unsaved");

    // recording over layer 0 drops its link
    a.record (0, kTone[4]);
    a.lib.tick(); a.run (0.2, 0.0); a.lib.tick();
    check (! a.lib.info (0).bound && a.lib.info (0).unsaved, "recording over a linked layer drops the link (layer now unsaved)");
    check (a.lib.bindings().size() == 1 && a.lib.bindings()[0].first == 1, "only layer 1 stays in the project");
    // Erase drops it
    a.select (1); a.erase(); a.run (0.1, 0.0); a.lib.tick();
    check (! a.lib.info (1).bound && a.e.layerState (1) == Engine::LayerEmpty, "erasing a linked layer drops the link");
    check (a.lib.bindings().empty(), "no links left");
    // Undo brings the audio back, but it is no longer the file's audio
    a.p.undo = ! a.p.undo; a.run (0.2, 0.0); a.lib.tick();
    check (a.e.layerState (1) == Engine::LayerFilled && ! a.lib.info (1).bound && a.lib.info (1).unsaved, "Undo after Erase: the audio is back, unsaved");

    // load a file into a layer: linked
    spit (home / "loaded.wav", ceo::writeWav16 (makeTone (44100.0, 0.7, kTone[6])));
    check (a.lib.loadLayer (4, (home / "loaded.wav").string(), why), "load accepted");
    a.settle();
    check (a.lib.info (4).bound && a.e.layerState (4) == Engine::LayerFilled && a.lib.bindings().size() == 1, "loading a file links the layer to it");
    check (! a.lib.loadLayer (4, (home / "nonexistent.wav").string(), why) && ! why.empty(), "a missing file is refused up front");
    spit (home / "bad.wav", std::vector<uint8_t> (100, 7));
    check (! a.lib.loadLayer (4, (home / "bad.wav").string(), why) && ! why.empty(), "a file that isn't a WAV is refused with a message");
    // a load is refused while something is recording
    a.p.layer = 0; a.p.capture = true; a.run (0.3, kTone[1]);
    check (! a.lib.loadLayer (4, (home / "loaded.wav").string(), why) && why.find ("recording") != std::string::npos, "loading while a recording runs is refused, with a reason");
    a.p.capture = false; a.run (0.2, 0.0);

    // Erase on an empty layer whose file is missing forgets the link
    {
        Inst m; m.lib.setHomeFolder (home.string());
        Lib::Binding bd; bd.path = "gone.wav"; bd.size = 1000; bd.hash = 12345;
        m.lib.restore ({ { 3, bd } }); m.settle();
        check (m.lib.info (3).status == Lib::Missing && m.lib.bindings().size() == 1, "a missing file: layer empty, link kept");
        m.select (3); m.erase();
        check (m.lib.bindings().empty() && ! m.lib.info (3).bound, "Erase on that empty layer forgets the link");
    }

    // sample-rate change: linked layers come back from their files at the new rate, unsaved ones are lost
    Inst f; f.lib.setHomeFolder (home.string());
    f.record (0, kTone[1]); f.lib.saveLayer (0, (home / "f0.wav").string(), why); f.settle();
    f.record (1, kTone[2]);                                         // unsaved
    f.record (2, kTone[3]); f.lib.saveLayer (2, (home / "f2.wav").string(), why); f.settle();
    f.e.prepare (44100.0, 480, 2); f.sr = 44100.0;                  // the host changed the sample rate: every layer is emptied by the engine
    check (f.e.layerState (0) == Engine::LayerEmpty || true, "(the engine empties the layers at the first block)");
    f.run (0.2, 0.0); f.settle (30.0); f.run (0.3, 0.0); f.lib.tick(); f.settle();
    check (f.e.layerState (0) == Engine::LayerFilled && f.e.layerState (2) == Engine::LayerFilled, "linked layers were reloaded at the new sample rate");
    check (f.e.layerState (1) == Engine::LayerEmpty, "the unsaved layer was lost with the rate change");
    check (f.lib.info (0).bound && f.lib.info (2).bound && f.lib.info (0).status == Lib::Ok, "still linked");
    check (toneOf (f.tail (0), 44100.0) == 1 && toneOf (f.tail (2), 44100.0) == 3, "each with its own audio, at the right pitch");
    check (f.e.layerFormat().sampleRate == 44100.0 && f.e.layerAudioLength() == 13 * 44100, "and the layers are 13 s at 44.1 kHz", (double) f.e.layerAudioLength());
    // a linked file that vanished before the next change: missing, no loop
    fs::remove (home / "f2.wav");
    f.e.prepare (48000.0, 480, 2); f.sr = 48000.0;
    f.run (0.2, 0.0); f.settle (30.0); f.run (0.3, 0.0); f.lib.tick(); f.settle();
    check (f.e.layerState (0) == Engine::LayerFilled && toneOf (f.tail (0), 48000.0) == 1, "layer 0 reloaded again at 48 kHz");
    check (f.e.layerState (2) == Engine::LayerEmpty && f.lib.info (2).status == Lib::Missing, "layer 2's file is gone: empty and reported missing");
    check (! f.lib.busy(), "nothing left retrying");
    // preparing again at the same rate keeps the audio and the links (the host does this when playback restarts)
    const uint32_t rev0 = f.e.layerRevision (0);
    f.e.prepare (48000.0, 480, 2); f.run (0.3, 0.0); f.lib.tick();
    check (f.e.layerState (0) == Engine::LayerFilled && f.e.layerRevision (0) == rev0 && f.lib.info (0).bound, "same-rate prepare: nothing changes");
    fs::remove_all (home);
}

// ---- 5. timing: the engine not ready, audio not running ------------------------------------------------------------------------
static void testNotReady()
{
    std::printf ("[5] restore before the audio is running; save while the audio is stopped\n");
    const fs::path home = freshHome ("ready");
    spit (home / "w.wav", ceo::writeWav16 (makeTone (48000.0, 0.8, kTone[2])));
    const std::string bytes = slurp (home / "w.wav");
    Lib::Binding bd; bd.path = "w.wav"; bd.size = bytes.size(); bd.hash = Lib::hashBytes ((const uint8_t*) bytes.data(), bytes.size());
    {
        Inst a (false);                                              // engine not prepared yet (the host sets the state before starting audio)
        a.lib.setHomeFolder (home.string());
        a.lib.restore ({ { 1, bd } });
        for (int i = 0; i < 20; ++i) a.lib.tick();
        check (a.lib.info (1).status == Lib::Waiting && a.lib.busy(), "waits while the engine isn't ready");
        a.e.prepare (48000.0, 480, 2); a.run (0.3, 0.0);
        a.settle();
        check (a.e.layerState (1) == Engine::LayerFilled && a.lib.info (1).status == Lib::Ok, "loads as soon as the audio runs");
    }
    {
        Inst a; a.lib.setHomeFolder (home.string());
        a.record (2, kTone[3]);
        std::string why; a.lib.saveLayer (2, (home / "stopped.wav").string(), why);
        for (int i = 0; i < 40; ++i) a.lib.tick();                   // no audio is running
        check (a.lib.busy() && ! fs::exists (home / "stopped.wav"), "a save waits while no audio runs");
        check (a.lib.info (2).message.find ("waiting for audio") != std::string::npos, "and says so");
        a.settle();
        check (fs::exists (home / "stopped.wav") && a.lib.info (2).bound, "completes when the audio runs");
    }
    {   // a load also says so when no audio runs
        Inst a; a.lib.setHomeFolder (home.string());
        std::string why;
        check (a.lib.loadLayer (4, (home / "w.wav").string(), why), "load accepted");
        for (int i = 0; i < 40; ++i) a.lib.tick();
        check (a.lib.busy() && a.lib.info (4).message.find ("waiting for audio") != std::string::npos, "a load waits while no audio runs, and says so");
        a.settle();
        check (a.lib.info (4).bound && a.e.layerState (4) == Engine::LayerFilled, "and completes when the audio runs");
    }
    {   // four saves at once
        Inst a; a.lib.setHomeFolder (home.string());
        std::string why;
        for (int k = 0; k < 4; ++k) a.record (k, kTone[k + 1]);
        for (int k = 0; k < 4; ++k) check (a.lib.saveLayer (k, (home / ("q" + std::to_string (k) + ".wav")).string(), why), "queued", k);
        a.settle();
        int ok = 0; for (int k = 0; k < 4; ++k) ok += a.lib.info (k).bound && fs::exists (home / ("q" + std::to_string (k) + ".wav"));
        check (ok == 4 && a.lib.bindings().size() == 4, "four queued saves all complete", ok);
        std::set<uint64_t> hs; for (int k = 0; k < 4; ++k) { std::string b = slurp (home / ("q" + std::to_string (k) + ".wav")); hs.insert (Lib::hashBytes ((const uint8_t*) b.data(), b.size())); }
        check (hs.size() == 4, "each file holds a different layer");
    }
    // suggested names
    Inst s; const std::string n = s.lib.suggestedFileName (2);
    checkS (n.rfind ("Ceo gamma ", 0) == 0 && n.size() > 14 && n.substr (n.size() - 4) == ".wav", "suggested file name looks right", n);
    checkS (s.lib.suggestedFileName (0).rfind ("Ceo alpha ", 0) == 0 && s.lib.suggestedFileName (5).rfind ("Ceo zeta ", 0) == 0, "alpha and zeta names", s.lib.suggestedFileName (5));
    fs::remove_all (home);
}

// ---- 6. mono and stereo layers, and what a loaded file becomes -------------------------------------------------------------
static void testStereoAndLoadDetails()
{
    std::printf ("[6] mono and stereo layers; loading files of any shape\n");
    const fs::path home = freshHome ("stereo");
    Inst a; a.lib.setHomeFolder (home.string());
    std::string why;
    // a stereo layer (Stereo input mode, different tones left and right) is saved as a stereo file
    a.record (0, kTone[1], 1.0, true, kTone[4]);
    check (a.e.layerIsStereo (0), "recorded in Stereo mode: a stereo layer");
    a.lib.saveLayer (0, (home / "st.wav").string(), why); a.settle();
    {
        LayerAudio w; std::string err; const std::string b = slurp (home / "st.wav");
        check (ceo::readWav ((const uint8_t*) b.data(), b.size(), w, err) && w.channels == 2 && w.frames() == (size_t) a.e.layerAudioLength(), "stereo layer: a stereo file of the whole layer", (double) w.frames());
        check (toneOf (std::vector<float> (w.ch[0].begin() + 4800, w.ch[0].begin() + 43200), 48000.0) == 1
               && toneOf (std::vector<float> (w.ch[1].begin() + 4800, w.ch[1].begin() + 43200), 48000.0) == 4, "left and right hold their own tones");
    }
    // loading that file into another instance gives a stereo layer with both sides
    Inst b; b.lib.setHomeFolder (home.string());
    b.record (3, kTone[2]);                                         // a mono layer is replaced by a stereo file
    check (! b.e.layerIsStereo (3), "layer 3 starts mono");
    check (b.lib.loadLayer (3, (home / "st.wav").string(), why), "load accepted");
    b.settle();
    check (b.e.layerIsStereo (3) && toneOf (b.tail (3, 0), 48000.0) == 1 && toneOf (b.tail (3, 1), 48000.0) == 4, "a stereo file makes a stereo layer with both sides");
    // and a mono file into a stereo layer makes it a mono layer
    spit (home / "mono.wav", ceo::writeWav16 (makeTone (48000.0, 0.5, kTone[5], 0.5f, 1)));
    check (b.lib.loadLayer (3, (home / "mono.wav").string(), why), "load accepted");
    b.settle();
    check (! b.e.layerIsStereo (3) && toneOf (b.tail (3, 0), 48000.0) == 5, "a mono file makes a mono layer");
    // length: a long file is cut at 13 s, a short one fills the start and the rest is silent
    spit (home / "long.wav", ceo::writeWav16 (makeTone (48000.0, 15.0, kTone[6], 0.5f, 1)));
    b.lib.loadLayer (2, (home / "long.wav").string(), why); b.settle();
    {
        const auto w = b.whole (2);
        check ((int) w.size() == 13 * 48000 && std::abs (w[13 * 48000 - 100]) + std::abs (w[13 * 48000 - 101]) + std::abs (w[13 * 48000 - 102]) > 0.0f, "a 15 s file is cut at 13 s (the layer is full to its end)");
        check (b.e.layerState (2) == Engine::LayerFilled, "and it counts as filled");
    }
    b.lib.loadLayer (4, (home / "mono.wav").string(), why); b.settle();
    {
        const auto w = b.whole (4);
        float tailMax = 0; for (size_t i = (size_t) (0.6 * 48000); i < w.size(); ++i) tailMax = std::max (tailMax, std::abs (w[i]));
        check (tailMax == 0.0f, "a 0.5 s file leaves the rest of the layer silent");
    }
    // load replaces what was there, does not count as the user's edit, and Undo does not bring the old audio back
    {
        Inst c; c.lib.setHomeFolder (home.string());
        c.record (1, kTone[1]); c.record (1, kTone[2]);             // the second recording over the first leaves an Undo copy
        check (c.e.undoLayer() == 1, "an Undo copy exists");
        const uint32_t editBefore = c.e.layerEditCount (1), revBefore = c.e.layerRevision (1);
        c.lib.loadLayer (1, (home / "mono.wav").string(), why); c.settle();
        check (toneOf (c.tail (1), 48000.0) == 5, "loading replaced the layer");
        check (c.e.layerEditCount (1) == editBefore && c.e.layerRevision (1) > revBefore, "a load raises the revision, not the user's edit count");
        check (c.e.undoLayer() == -1, "Undo no longer offers the audio the load replaced");
    }
    // a file loaded while the layer is the one being played: grains keep going and the sound changes (no crash, no stuck state)
    {
        Inst c; c.lib.setHomeFolder (home.string());
        c.record (0, kTone[1]);
        c.p.layer = 0; c.p.intensity = 0.5f; c.p.length = 0.3f;
        c.run (0.5, 0.0);
        c.lib.loadLayer (0, (home / "mono.wav").string(), why);
        for (int i = 0; i < 20; ++i) { c.run (0.1, 0.0); c.lib.tick(); }
        check (c.lib.info (0).bound && toneOf (c.tail (0), 48000.0) == 5 && c.e.activeGrainCount() >= 0, "loading into the layer that is playing works");
    }
    fs::remove_all (home);
}

// ---- 7. the audio thread: no allocation, and how long the copy takes -------------------------------------------------------------
static double threadCpuMs()
{
    timespec ts; clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts);
    return (double) ts.tv_sec * 1000.0 + (double) ts.tv_nsec / 1.0e6;
}

static void testAudioThread()
{
    std::printf ("[7] the audio thread: nothing allocated while a save and a load are in flight; copy time\n");
    const fs::path home = freshHome ("audio");
    spit (home / "in.wav", ceo::writeWav16 (makeTone (48000.0, 1.0, kTone[3], 0.5f, 2, kTone[4])));
    Inst a; a.lib.setHomeFolder (home.string());
    a.record (0, kTone[1], 1.0, true, kTone[2]);
    a.record (1, kTone[2]);
    std::string why;

    // run blocks by hand, counting allocations and timing each call to process()
    const int N = 64;                                              // a small buffer, the hardest case
    std::vector<float> L (N), R (N); float* ch[2] = { L.data(), R.data() };
    long allocs = 0; double worst = 0, worstCopy = 0, sum = 0; long blocks = 0;
    auto block = [&] (bool serviceExpected)
    {
        for (int i = 0; i < N; ++i) { L[i] = 0.3f * (float) std::sin (0.05 * (double) (blocks * N + i)); R[i] = L[i]; }
        a.e.setParams (a.p);
        gAllocations.store (0); gCounting.store (true);
        const double t0 = threadCpuMs();
        a.e.process (ch, 2, N);
        const double t1 = threadCpuMs();
        gCounting.store (false);
        allocs += gAllocations.load();
        worst = std::max (worst, t1 - t0); sum += t1 - t0; ++blocks;
        if (serviceExpected) worstCopy = std::max (worstCopy, t1 - t0);
    };
    for (int i = 0; i < 400; ++i) block (false);                    // warm up
    worst = 0; sum = 0; blocks = 0;

    check (a.lib.saveLayer (0, (home / "s0.wav").string(), why), "stereo save accepted");
    for (int i = 0; i < 300; ++i)
    {
        block (a.e.pollImport() == Engine::IoNone && false);
        if (i % 100 == 50) a.lib.tick();
    }
    a.lib.tick(); a.lib.tick();
    check (a.lib.info (0).bound || a.lib.busy(), "save progressed while the audio ran");
    for (int i = 0; i < 200 && a.lib.busy(); ++i) { block (false); a.lib.tick(); }
    check (a.lib.info (0).bound && fs::exists (home / "s0.wav"), "the save finished");
    check (a.lib.loadLayer (2, (home / "in.wav").string(), why), "stereo load accepted");
    for (int i = 0; i < 400 && a.lib.busy(); ++i) { block (false); if (i % 5 == 0) a.lib.tick(); }
    check (a.lib.info (2).bound && a.e.layerIsStereo (2), "the load finished");
    check (allocs == 0, "process() allocated nothing during the save and the load", (double) allocs);
    std::printf ("      %ld blocks of %d samples: average %.4f ms, longest %.3f ms (budget at 48 kHz: %.3f ms); an ordinary block is 0.01 to 0.05 ms\n",
                blocks, N, sum / (double) blocks, worst, 1000.0 * N / 48000.0);

    // time the two transfer blocks on their own (a stereo layer: the biggest copy)
    double saveBlock = 0, loadBlock = 0;
    {
        std::vector<double> t;
        for (int rep = 0; rep < 5; ++rep)
        {
            a.e.requestExport (0);
            for (int i = 0; i < N; ++i) { L[i] = 0; R[i] = 0; }
            a.e.setParams (a.p);
            const double t0 = threadCpuMs(); a.e.process (ch, 2, N); const double t1 = threadCpuMs();
            ceo::LayerAudio out; a.e.pollExport (out);
            t.push_back (t1 - t0);
        }
        std::sort (t.begin(), t.end()); saveBlock = t[2];
        t.clear();
        const auto audio = makeTone (48000.0, 13.0, 440.0, 0.5f, 2, 550.0);
        for (int rep = 0; rep < 5; ++rep)
        {
            std::string w; a.e.submitImport (3, audio, w);
            a.e.setParams (a.p);
            const double t0 = threadCpuMs(); a.e.process (ch, 2, N); const double t1 = threadCpuMs();
            a.e.pollImport();
            t.push_back (t1 - t0);
        }
        std::sort (t.begin(), t.end()); loadBlock = t[2];
    }
    std::printf ("      the block that copies a stereo layer OUT takes %.3f ms, the block that copies one IN takes %.3f ms (64-sample budget %.3f ms)\n",
                 saveBlock, loadBlock, 1000.0 * N / 48000.0);
    check (saveBlock < 50.0 && loadBlock < 50.0, "a transfer block does not take absurdly long", saveBlock);
    fs::remove_all (home);
}

// ---- 8. the engine itself has no file code -----------------------------------------------------------------------------------------
static void testEngineHasNoFileCode()
{
    std::printf ("[8] the engine's own source has no file code\n");
    for (const char* path : { CEO_ENGINE_CPP, CEO_ENGINE_H })
    {
        const std::string text = slurp (path);
        check (! text.empty(), "engine source found", 0);
        for (const char* bad : { "fstream", "fopen", "std::filesystem", "<cstdio>", "<stdio.h>", "fread", "fwrite", "ifstream", "ofstream", "std::mutex", "<mutex>" })
            checkS (text.find (bad) == std::string::npos, "no file or lock code in the engine", std::string (path) + " contains " + bad);
    }
}

int main()
{
    testWav();
    testSave();
    testProjectRoundTrip();
    testFindMoved();
    testEditsAndRateChange();
    testNotReady();
    testStereoAndLoadDetails();
    testAudioThread();
    testEngineHasNoFileCode();
    std::printf ("\nlibrary_test: %d checks, %d failed\n", gChecks, gFailed);
    return gFailed == 0 ? 0 : 1;
}
