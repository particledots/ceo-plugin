// Tests of Scenes: reading Arbhar's "#ARB" preset files (including real-world damage: wrongly closed comments, a stray comment close),
// Ceo's own scene settings file, picking which WAV goes to which layer, the Load menu rules, the Arbhar quantise table -> Pitch Scale,
// loading six real WAV files at once (24-bit stereo, like the official 4_4_scene folder), and saving a Scene without ever deleting a file.
// What it does NOT check: the panel, the menus and dialogs, the host's project handling, or how anything sounds in Logic.
#include "ceo/Engine.h"
#include "ceo/LayerAudio.h"
#include "ceo/LayerLibrary.h"
#include "ceo/Scene.h"
#include "ceo/Scales.h"

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

namespace sc = ceo::scene;

static std::string slurp (const fs::path& p) { std::ifstream f (p, std::ios::binary); return { std::istreambuf_iterator<char> (f), {} }; }
static void spitS (const fs::path& p, const std::string& s) { fs::create_directories (p.parent_path()); std::ofstream f (p, std::ios::binary | std::ios::trunc); f << s; }
static fs::path freshHome (const char* name)
{
    const fs::path h = fs::temp_directory_path() / ("ceoscene_test_" + std::to_string ((long) getpid()) + "_" + name);
    fs::remove_all (h); fs::create_directories (h);
    return h;
}
static bool has (const std::vector<std::string>& v, const std::string& s) { return std::find (v.begin(), v.end(), s) != v.end(); }

// A 24-bit stereo PCM WAV (what the official Arbhar sample files are): a tone, slightly different in the two channels.
static std::string make24 (double seconds, double rate, double hz, double hzR)
{
    const uint32_t frames = (uint32_t) (seconds * rate), dataBytes = frames * 6;
    std::string o;
    auto u32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) o += (char) ((v >> (8 * i)) & 0xff); };
    auto u16 = [&] (uint16_t v) { o += (char) (v & 0xff); o += (char) (v >> 8); };
    o += "RIFF"; u32 (36 + dataBytes); o += "WAVEfmt "; u32 (16); u16 (1); u16 (2); u32 ((uint32_t) rate); u32 ((uint32_t) rate * 6); u16 (6); u16 (24);
    o += "data"; u32 (dataBytes);
    for (uint32_t i = 0; i < frames; ++i)
        for (double f : { hz, hzR })
        {
            const int32_t v = (int32_t) std::lrint (0.4 * 8388607.0 * std::sin (2.0 * kPi * f * (double) i / rate));
            o += (char) (v & 0xff); o += (char) ((v >> 8) & 0xff); o += (char) ((v >> 16) & 0xff);
        }
    return o;
}

// What the real MidSide.txt looks like: #ARB, comments, a stray close, wrongly closed comment blocks, one { } block.
static const char* kMidSide =
    "#ARB\n"
    "/* arbhar preset file\n"
    "   Load Configuration: 0 nothing 1 preset 2 layers 3 scene\n"
    " */\n"
    " */\n"
    "{\n"
    "/* Load Configuration\n"
    "   (a comment that is closed wrongly)\n"
    "/*\n"
    "PRESET_NAME: \"Mid Side\"\n"
    "PARAMETER: LoadConfiguration: 2\n"
    "/* Input mode: 0 mono, 1 stereo\n"
    "/*\n"
    "PARAMETER: InputMode: 1\n"
    "PARAMETER: AnalogEmulation: 1\n"
    "PARAMETER: PhaseSwitch: 1\n"
    "PARAMETER: CaptureCVMode: 1\n"
    "PARAMETER: CaptureButtonMode: 0\n"
    "PARAMETER: OnsetMode: 1\n"
    "PARAMETER: FollowMode: 0\n"
    "PARAMETER: FollowSpeedDirection: 1\n"
    "PARAMETER: ModCV: 1\n"
    "PARAMETER: WavetableCentreFrequency: 130.813\n"
    "PARAMETER: QuantiseTable: \"1 -2 2 -1 3 -4 4 -3 5 -7 6 -6 7 -5 8 -8 9 -10 10 -9 11 -12 12 -11\"\n"
    "}\n";

static void testParser()
{
    std::printf ("[1] reading preset files\n");
    auto p = sc::parsePresetText (kMidSide);
    check (p.valid && ! p.ceoFile, "MidSide-style file is recognised as an Arbhar preset");
    checkS (p.name == "Mid Side", "preset name", p.name);
    check (p.loadConfiguration == 2, "Load Configuration 2 (layers only)", p.loadConfiguration);
    check (p.s.inputMode == 1, "InputMode 1 -> Stereo", p.s.inputMode);
    check (p.s.modTarget == 0, "ModCV 1 (Panning) -> Pan", p.s.modTarget);
    check (p.s.follow == 0, "FollowMode 0 (Scan mode) -> off", p.s.follow);
    check (p.s.onsetCapture == -1, "OnsetMode 1 (alpha) does NOT switch Onset capture on (it would record over the loaded layers)", p.s.onsetCapture);
    {   bool noted = false; for (const auto& n : p.notes) if (n.find ("Onset capture") != std::string::npos && n.find ("overwrite") != std::string::npos) noted = true;
        check (noted, "and the report says why"); }
    for (const auto& line : p.set) check (line.find ("Onset") == std::string::npos, "Onset is not listed as set");
    check (p.s.pitchScale == ceo::scales::kNumScales, "the chromatic quantise table -> Pitch Scale Chromatic", p.s.pitchScale);
    check (has (p.ignored, "PhaseSwitch") && has (p.ignored, "AnalogEmulation") && has (p.ignored, "WavetableCentreFrequency") && has (p.ignored, "FollowSpeedDirection"),
           "settings Ceo has no use for are listed as ignored");
    check (! has (p.ignored, "InputMode") && ! has (p.ignored, "LoadConfiguration") && ! has (p.ignored, "ModCV"), "settings Ceo uses are not listed as ignored");
    check (p.set.size() == 4, "four settings reported as set (Onset is left alone)", (double) p.set.size());
    {   std::set<std::string> uniq (p.ignored.begin(), p.ignored.end()); check (uniq.size() == p.ignored.size(), "no name listed twice"); }

    // words, as the manual shows them; also the manual's own typo in "LoadConf iguration"
    p = sc::parsePresetText ("#ARB\nPRESET_NAME: arbhar Panning\nPARAMETER: LoadConf iguration: Load Preset\nPARAMETER: InputMode: Mono\nPARAMETER: OnsetMode: Disable\n"
                             "PARAMETER: FollowMode: Scan Mode\nPARAMETER: ModCV: Panning\nPARAMETER: QuantiseTable: Octaves\n");
    check (p.loadConfiguration == 1 && p.s.inputMode == 0 && p.s.onsetCapture == 0 && p.s.follow == 0 && p.s.modTarget == 0 && p.s.pitchScale == 0, "word values are understood");
    p = sc::parsePresetText ("#ARB\nPARAMETER: ModCV: Delay\nPARAMETER: FollowMode: Follow Mode\nPARAMETER: OnsetMode: beta\nPARAMETER: QuantiseTable: Natural Minor\n");
    check (p.s.modTarget == 3 && p.s.follow == 1 && p.s.onsetCapture == -1 && p.s.pitchScale == 6, "Delay, Follow Mode, beta (Onset left alone), Natural Minor");
    p = sc::parsePresetText ("#ARB\nPARAMETER: OnsetMode: 2\n");
    check (p.s.onsetCapture == -1 && ! p.notes.empty(), "OnsetMode 2 (beta) is left alone too");
    p = sc::parsePresetText ("#ARB\nPARAMETER: OnsetMode: 0\n");
    check (p.s.onsetCapture == 0, "OnsetMode 0 still switches Onset capture off");
    p = sc::parsePresetText ("#ARB\nPARAMETER: OnsetMode: Enable\nPARAMETER: InputMode: Mono\n");
    check (p.s.onsetCapture == -1 && p.s.inputMode == 0, "the word Enable is left alone as well");
    p = sc::parsePresetText ("#CEO-SCENE 1\nPARAMETER: OnsetMode: Enable\n");
    check (p.ceoFile && p.s.onsetCapture == 1, "Ceo's own scene file brings back Onset capture as it was saved");
    p = sc::parsePresetText ("#ARB\nPARAMETER: ModCV: 0\nPARAMETER: OnsetMode: delta\n");
    check (p.s.modTarget == -1 && p.s.onsetCapture == -1 && p.notes.size() == 2, "Mod CV none and an onset mode Ceo doesn't have are left alone, with a note", (double) p.notes.size());
    p = sc::parsePresetText ("\xef\xbb\xbf#ARB\r\nPARAMETER: ModCV: 4\r\n");
    check (p.valid && p.s.modTarget == 3, "a byte-order mark and Windows line ends are fine");
    p = sc::parsePresetText ("hello\nPARAMETER: ModCV: 4\n");
    check (! p.valid && ! p.s.any(), "a text file that isn't a preset is not read");
    p = sc::parsePresetText ("#ARB\n");
    check (p.valid && ! p.s.any() && p.loadConfiguration == -1, "an empty preset is valid but sets nothing");

    // Ceo's own file round trip
    sc::Settings s; s.inputMode = 0; s.modTarget = 2; s.follow = 1; s.onsetCapture = 1; s.pitchSnap = 2; s.pitchScale = 7;
    const std::string text = sc::writeCeoSettings ("My scene", s);
    p = sc::parsePresetText (text);
    check (p.valid && p.ceoFile && p.name == "My scene", "Ceo's own file is recognised, name kept");
    check (p.s.inputMode == 0 && p.s.modTarget == 2 && p.s.follow == 1 && p.s.onsetCapture == 1 && p.s.pitchSnap == 2 && p.s.pitchScale == 7, "every setting survives a write and a read");
    for (int sca = 0; sca <= ceo::scales::kNumScales; ++sca)
    {
        sc::Settings t; t.pitchScale = sca;
        const auto q = sc::parsePresetText (sc::writeCeoSettings ("x", t));
        check (q.s.pitchScale == sca, "every Pitch Scale name survives a write and a read", sca, q.s.pitchScale);
    }
    p = sc::parsePresetText (sc::writeCeoSettings ("none set", sc::Settings()));
    check (p.valid && ! p.s.any(), "a file that sets nothing reads back as nothing");
}


// The four official preset files have this structure and these PARAMETER lines (copied from the files the user supplied):
// a stray comment close near the top, comment blocks closed wrongly with "/*", indented PARAMETER lines, an unquoted QuantiseTable.
static std::string officialStyle (const char* name, int input, int modCv, const char* table)
{
    std::string t = "#ARB\n/*\n    Instru\xc5\x8d arbhar V2 Configuration\n    Please note that a valid arbhar preset file has to have the #ARB identification\n*/\n\n*/\n"
                    "    Some parameters are accessed via button and knob combinations (PARAM_TEMPORARY).\n*/\n\n\n{\n    /*\n    Preset Name\n    */\n\t\n";
    t += std::string ("   PRESET_NAME: \"") + name + "\"\n    \n\n    /*\n    Load Configuration\n        0 - Load Nothing\n        2 - Load Layers\n    */\n\nPARAMETER: LoadConfiguration: 2\n\n";
    t += "    /*\n    Mono and Stereo\n    PARAM_TEMPORARY: Press and hold the Shift and Capture Buttons\n    */\n\nPARAMETER: InputMode: " + std::to_string (input) + "\n\n";
    for (const char* l : { "AnalogEmulation: 1", "PhaseSwitch: 0", "CaptureCVMode: 1", "CaptureButtonMode: 0", "ActivateCaptureOnButtonUp: 0", "AccumulativeCaptureMode: 0",
                           "LinkAccumulativeRecordingCaptureAsGate: 0", "OnsetMode: 1", "StrikeButtonToTrigger: 0", "StrikeCVDelay: 10", "FollowMode: 0", "FollowSpeedDirection: 1" })
        t += std::string ("\n    /*\n    comment\n    */\n\nPARAMETER: ") + l + "\n\n";
    t += "\n    /*\n    Follow Mode Scan Offset\n        0 \xe2\x80\x93 0 ms (Default)\n    /*\t\n\nPARAMETER: FollowScanOffset: 0\n\n";
    t += "\n    /*\n    Follow Mode Scan Offset and Speed Control\n    /*\t\n\n   PARAMETER: FollowPositionOffsetWithScanCV: 1\n\n";
    for (const char* l : { "FollowLoop: 1", "FollowSetLoopLengthWithHold: 1", "RandomTimingWithRandomIntensity: 1", "RandomAmpWithRandomIntensity: 0" })
        t += std::string ("\n    /*\n    comment\n    /*\t\n\nPARAMETER: ") + l + "\n\n";
    t += "\n    /*\n    Mod CV Input Modes\n        0 - None\n        1 - Panning\n        3 - Reverb (Default)\n    */\n\nPARAMETER: ModCV: " + std::to_string (modCv) + "\n\n";
    t += "    /*\n    Centre frequency\n    */\n\nPARAMETER: WavetableCentreFrequency: 130.813\n\n\n    /*\n    Quantised Random Pitch Deviation Table\n        Chromatic:     1 -2 2 -1 3 -4\n    */\n\n";
    t += std::string ("PARAMETER: QuantiseTable: ") + table + "\n\n\n\n    ______________________________________\n\n\n}\n";
    return t;
}

static void testOfficialStyle()
{
    std::printf ("[1b] files shaped like the four official presets\n");
    const char* chromatic = "1 -2 2 -1 3 -4 4 -3 5 -7 6 -6 7 -5 8 -9 9 -8 10 -11 11 -10 12 -12";
    auto classic = sc::parsePresetText (officialStyle ("arbhar Classic", 0, 3, chromatic));
    check (classic.valid && classic.name == "arbhar Classic" && classic.loadConfiguration == 2, "arbharClassic: valid, name, Load Configuration 2");
    check (classic.s.inputMode == 0 && classic.s.modTarget == 2 && classic.s.follow == 0 && classic.s.onsetCapture == -1 && classic.s.pitchScale == ceo::scales::kNumScales,
          "arbharClassic: Mono, Reverb, Scan, Chromatic, Onset left alone");
    check (classic.ignored.size() == 17, "17 settings are listed as ignored", (double) classic.ignored.size());
    auto stereo = sc::parsePresetText (officialStyle ("Stereo", 1, 3, chromatic));
    check (stereo.s.inputMode == 1 && stereo.s.modTarget == 2, "Stereo: Stereo input, Reverb");
    auto mid = sc::parsePresetText (officialStyle ("Mid Side", 1, 1, chromatic));
    check (mid.s.inputMode == 1 && mid.s.modTarget == 0, "MidSide: Stereo input, Mod target Pan");
    auto intervals = sc::parsePresetText (officialStyle ("Intervals 3", 0, 3, "12 -12 -12 12 7 -5 -5 19"));
    check (intervals.name == "Intervals 3" && intervals.s.pitchScale >= 1, "Intervals 3: the octave-and-fifth table gives a scale");
    checkS (sc::pitchScaleName (intervals.s.pitchScale) == "Major Pentatonic", "Intervals 3 -> the scale named in the report", sc::pitchScaleName (intervals.s.pitchScale));
    // the whole thing through a real folder
    const fs::path home = freshHome ("official");
    spitS (home / "MidSide.txt", officialStyle ("Mid Side", 1, 1, chromatic));
    spitS (home / "1_x.wav", "x");
    const auto f = sc::scanFolder (home.string());
    const auto p = sc::loadPresetFile (f.settingsFile);
    check (p.valid && p.s.modTarget == 0, "read from a file on disk");
    const auto d = sc::decide (sc::AsFileSays, f, p);
    check (d.layers && ! d.settings, "as the file says: layers only, like Arbhar");
    const auto d2 = sc::decide (sc::SceneAll, f, p);
    check (d2.layers && d2.settings, "Load menu set to Scene: both");
    fs::remove_all (home);
}

static void testScale()
{
    std::printf ("[2] quantise table -> nearest Pitch Scale\n");
    check (sc::nearestScale ({ 12, -12 }) == 0, "octaves only -> Off");
    check (sc::nearestScale ({ 1, -2, 2, -1, 3, -4, 4, -3, 5, -7, 6, -6, 7, -5, 8, -8, 9, -10, 10, -9, 11, -12, 12, -11 }) == ceo::scales::kNumScales, "chromatic -> Chromatic");
    check (sc::nearestScale ({ 2, 4, 5, 7, 9, 11, 12 }) == 1, "major -> Major (Ionian)");
    check (sc::nearestScale ({ 2, 3, 5, 7, 8, 10, 12 }) == 6, "natural minor -> Aeolian");
    check (sc::nearestScale ({ 2, 3, 5, 7, 9, 10, 12 }) == 2, "dorian -> Dorian");
    check (sc::nearestScale ({ 2, 4, 6, 8, 10, 12 }) == 24, "whole tone -> Whole Tone");
    check (sc::nearestScale ({ 2, 4, 7, 9, 12 }) == 17, "major pentatonic -> Major Pentatonic");
    check (sc::nearestScale ({ 1, 3, 4, 6, 7, 9, 10, 12 }) == 26, "half-whole octatonic (a mode of limited transposition) -> Octatonic (Half-Whole)");
    check (sc::nearestScale ({ -5, 7 }) >= 0 && sc::nearestScale ({ -5, 7 }) <= ceo::scales::kNumScales, "negative steps and odd tables give a valid scale");
    check (sc::nearestScale ({}) == 0, "an empty table -> Off");
}

static void testScan()
{
    std::printf ("[3] which file goes to which layer\n");
    const fs::path home = freshHome ("scan");
    const char* names[6] = { "1_M_LR\xc3\x98_MM.wav", "2_M_LR\xc3\x98_OO.wav", "3_M_LR\xc3\x98_AA.wav", "4_M_LR\xc3\x98_EE.wav", "5_M_LR\xc3\x98_ALL.wav", "6_M_LR\xc3\x98_ALL_Faded.wav" };
    const fs::path d = home / "4_4_scene";
    for (auto* n : names) spitS (d / n, "x");
    spitS (d / "._1_M_LR\xc3\x98_MM.wav", "junk");
    spitS (d / "_was_old.wav", "old");
    spitS (d / "MidSide.txt", kMidSide);
    spitS (d / "notes.txt", "just some notes");
    auto f = sc::scanFolder (d.string());
    check (f.byDigit && f.audioCount == 6, "six files, picked by their leading digit", f.audioCount);
    bool right = true; for (int i = 0; i < 6; ++i) right = right && fs::path (f.audio[i]).filename().string() == names[i];
    check (right, "each digit goes to its own layer");
    checkS (fs::path (f.settingsFile).filename() == "MidSide.txt", "the preset file is found, other .txt files are not", f.settingsFile);
    checkS (f.name == "4_4_scene", "folder name", f.name);

    const fs::path d2 = home / "plain";
    for (auto* n : { "b.wav", "a.WAV", "c.wav", "d.wav", "e.wav", "f.wav", "g.wav", "h.wav" }) spitS (d2 / n, "x");
    f = sc::scanFolder (d2.string());
    check (! f.byDigit && f.audioCount == 6, "without digits the first six in alphabetical order are used", f.audioCount);
    check (fs::path (f.audio[0]).filename() == "a.WAV" && fs::path (f.audio[5]).filename() == "f.wav", "alphabetical, any letter case");
    check (f.settingsFile.empty(), "no settings file");

    const fs::path d3 = home / "gaps";
    for (auto* n : { "5_y.wav", "2_x.wav", "2_a_first.wav", "0_zero.wav", "7_seven.wav" }) spitS (d3 / n, "x");
    f = sc::scanFolder (d3.string());
    check (f.byDigit && f.audioCount == 2 && ! f.audio[1].empty() && ! f.audio[4].empty() && f.audio[0].empty(), "digit gaps leave layers empty, 0 and 7 are ignored", f.audioCount);
    check (fs::path (f.audio[1]).filename() == "2_a_first.wav", "two files with the same digit: the first alphabetically wins");

    const fs::path d4 = home / "both";
    spitS (d4 / "1_a.wav", "x"); spitS (d4 / "MidSide.txt", kMidSide); spitS (d4 / "Ceo scene.txt", sc::writeCeoSettings ("c", sc::Settings()));
    f = sc::scanFolder (d4.string());
    check (fs::path (f.settingsFile).filename() == "Ceo scene.txt", "Ceo's own settings file wins when both are there");

    check (sc::scanFolder ((home / "nothing here").string()).audioCount == 0, "a folder that doesn't exist holds nothing");
    spitS (home / "scenes" / "B scene" / "1.wav", "x"); spitS (home / "scenes" / "a scene" / "1.wav", "x"); spitS (home / "scenes" / ".hidden" / "1.wav", "x"); spitS (home / "scenes" / "loose.wav", "x");
    const auto l = sc::listScenes ((home / "scenes").string());
    check (l.size() == 2 && fs::path (l[0]).filename() == "a scene" && fs::path (l[1]).filename() == "B scene", "the Scenes menu lists sub-folders, alphabetical, no hidden ones, no loose files", (double) l.size());
    fs::remove_all (home);
}

static void testDecide()
{
    std::printf ("[4] the Load menu rules\n");
    sc::SceneFiles both; both.audioCount = 6; both.settingsFile = "/x/MidSide.txt";
    sc::SceneFiles audioOnly; audioOnly.audioCount = 6;
    sc::SceneFiles settingsOnly; settingsOnly.settingsFile = "/x/p.txt";
    sc::SceneFiles none;
    auto pre = [] (int cfg, bool usable = true) { sc::Preset p; p.valid = true; p.loadConfiguration = cfg; if (usable) p.s.modTarget = 0; return p; };
    using W = sc::LoadWhat;
    auto d = sc::decide (W::AsFileSays, both, pre (2));  check (d.layers && ! d.settings, "as the file says: 2 = layers only");
    d = sc::decide (W::AsFileSays, both, pre (3));       check (d.layers && d.settings, "as the file says: 3 = both");
    d = sc::decide (W::AsFileSays, both, pre (1));       check (! d.layers && d.settings, "as the file says: 1 = settings only");
    d = sc::decide (W::AsFileSays, both, pre (0));       check (! d.layers && ! d.settings && d.note.find ("override") != std::string::npos, "as the file says: 0 = nothing, and it says how to override");
    d = sc::decide (W::AsFileSays, both, pre (-1));      check (d.layers && d.settings, "no Load Configuration in the file: both");
    d = sc::decide (W::AsFileSays, audioOnly, sc::Preset());   check (d.layers && ! d.settings, "no settings file: layers only");
    d = sc::decide (W::AsFileSays, settingsOnly, pre (2));     check (! d.layers && ! d.settings && ! d.note.empty(), "the file says layers but there are none: nothing, with a reason");
    d = sc::decide (W::AsFileSays, settingsOnly, pre (-1));    check (! d.layers && d.settings, "no WAV files but settings: settings only");
    d = sc::decide (W::AsFileSays, none, sc::Preset());        check (! d.layers && ! d.settings && d.note.find ("Nothing to load") != std::string::npos, "neither: Nothing to load");
    d = sc::decide (W::LayersOnly, both, pre (0));       check (d.layers && ! d.settings, "Layers only overrides a file that says nothing");
    d = sc::decide (W::SettingsOnly, both, pre (2));     check (! d.layers && d.settings, "Settings only overrides a file that says layers only");
    d = sc::decide (W::SceneAll, both, pre (2));         check (d.layers && d.settings, "Scene overrides a file that says layers only");
    d = sc::decide (W::SceneAll, audioOnly, sc::Preset()); check (d.layers && ! d.settings, "Scene with no settings loads the layers");
    d = sc::decide (W::SettingsOnly, audioOnly, sc::Preset()); check (! d.settings && ! d.note.empty(), "Settings only with no settings file says so");
    d = sc::decide (W::LayersOnly, settingsOnly, pre (3));    check (! d.layers && ! d.note.empty(), "Layers only with no WAV files says so");
    d = sc::decide (W::SceneAll, both, pre (3, false));  check (d.layers && ! d.settings, "a preset with nothing Ceo can use sets nothing");
}

static void testLoadMany()
{
    std::printf ("[5] loading a whole Scene (six 24-bit stereo files, 13 s)\n");
    const fs::path home = freshHome ("loadmany");
    const fs::path d = home / "4_4_scene";
    std::vector<std::pair<int, std::string>> list;
    for (int i = 0; i < 6; ++i)
    {
        const fs::path f = d / (std::to_string (i + 1) + "_M_LR\xc3\x98_" + std::to_string (i) + ".wav");
        spitS (f, make24 (13.0, 48000.0, kTone[i + 1], kTone[i + 1]));
        list.push_back ({ i, f.string() });
    }
    check (fs::file_size (list[0].second) == 3750044 || fs::file_size (list[0].second) > 3700000, "the test files are the size of the official ones (about 3.75 MB)", (double) fs::file_size (list[0].second));
    Inst a; a.lib.setHomeFolder (home.string());
    std::string why;
    check (a.lib.loadMany (list, why), "loading six files is accepted");
    check (a.lib.busy(), "busy while loading");
    a.settle();
    bool all = true;
    for (int i = 0; i < 6; ++i)
    {
        const auto info = a.lib.info (i);
        all = all && info.bound && info.status == Lib::Ok && a.e.layerState (i) == Engine::LayerFilled && toneOf (a.tail (i), 48000.0) == i + 1;
        checkS (info.fullPath == list[(size_t) i].second, "the layer knows where its file is", info.fullPath);
    }
    check (all, "all six layers hold their own tone and are bound to their files");
    check (a.lib.bindings().size() == 6, "six bindings for the project", (double) a.lib.bindings().size());

    // a Scene with one bad file loads nothing at all
    Inst b; b.lib.setHomeFolder (home.string());
    spitS (d / "bad.wav", "this is not a wav file");
    auto bad = list; bad[3].second = (d / "bad.wav").string();
    check (! b.lib.loadMany (bad, why) && why.find ("bad.wav") != std::string::npos && why.find ("Nothing was loaded") != std::string::npos, "one unreadable file: refused, naming the file");
    b.settle();
    bool empty = true; for (int i = 0; i < 6; ++i) empty = empty && b.e.layerState (i) == Engine::LayerEmpty && ! b.lib.info (i).bound;
    check (empty && ! b.lib.busy(), "and no layer was touched");
    bad[3].second = (d / "missing.wav").string();
    check (! b.lib.loadMany (bad, why), "a missing file: refused");
    check (! b.lib.loadMany ({}, why), "an empty list: refused");
    // while a recording runs
    b.p.layer = 0; b.p.capture = true; b.run (0.3, kTone[1]);
    check (! b.lib.loadMany (list, why) && why.find ("recording") != std::string::npos, "refused while recording");
    b.p.capture = false; b.run (0.3, 0.0);
    // a Scene with fewer files leaves the other layers alone
    Inst c; c.lib.setHomeFolder (home.string());
    c.record (0, kTone[1]);
    check (c.lib.loadMany ({ list[2], list[4] }, why), "two files accepted");
    c.settle();
    check (c.e.layerState (0) == Engine::LayerFilled && c.lib.info (0).unsaved && ! c.lib.info (2).unsaved && c.lib.info (2).bound && c.lib.info (4).bound && c.e.layerState (1) == Engine::LayerEmpty,
          "layers with no file in the Scene keep what they had");
    check (toneOf (c.tail (0), 48000.0) == 1 && toneOf (c.tail (2), 48000.0) == 3, "the right tones");
    // a layer that is busy
    check (c.lib.loadMany ({ list[1] }, why) && ! c.lib.loadMany ({ list[1] }, why), "a layer with a load in progress refuses another");
    c.settle();
    fs::remove_all (home);
}

static void testSaveScene()
{
    std::printf ("[6] saving a Scene: nothing is ever deleted\n");
    const fs::path home = freshHome ("savescene");
    const fs::path d = home / "My scene";
    Inst a; a.lib.setHomeFolder (home.string());
    a.record (0, kTone[1]); a.record (2, kTone[3]); a.record (4, kTone[5]);
    std::string why;
    // things already in the folder
    spitS (d / "1_alpha.wav", "OLD ALPHA"); spitS (d / "2_something.wav", "OLD TWO"); spitS (d / "MidSide.txt", kMidSide); spitS (d / "Ceo scene.txt", "#CEO-SCENE 1\nOLD SETTINGS\n");
    spitS (d / "_was_20200101-0000_keep.wav", "ALREADY RENAMED"); spitS (d / "readme.md", "notes");
    const std::string before = slurp (d / "MidSide.txt");

    sc::Settings s; s.inputMode = 1; s.modTarget = 0; s.pitchScale = 6;
    check (a.lib.saveScene (d.string(), sc::writeCeoSettings ("My scene", s), why), "save accepted");
    check (! a.lib.saveScene (d.string(), "x", why), "a second Scene save while busy is refused");
    a.settle();
    Lib::SceneSaved r;
    check (a.lib.takeSceneSaved (r) && r.ok, "the Scene save finished, ok");
    checkS (r.message.find ("3 WAV files") != std::string::npos && r.message.find ("3 older files renamed") != std::string::npos, "the message says what happened", r.message);
    check (r.layers == std::vector<int> ({ 0, 2, 4 }), "it saved layers alpha, gamma and epsilon");
    check (! a.lib.takeSceneSaved (r), "the result is handed over once");
    check (fs::exists (d / "1_alpha.wav") && fs::exists (d / "3_gamma.wav") && fs::exists (d / "5_epsilon.wav") && ! fs::exists (d / "2_beta.wav"), "only the filled layers were written");
    check (slurp (d / "MidSide.txt") == before, "Arbhar's own preset file is untouched");
    check (slurp (d / "readme.md") == "notes" && slurp (d / "_was_20200101-0000_keep.wav") == "ALREADY RENAMED", "other files are untouched");
    int was = 0; bool oldAlpha = false, oldTwo = false, oldSet = false, tmp = false;
    for (auto& e : fs::directory_iterator (d))
    {
        const std::string n = e.path().filename().string();
        if (n.find (".ceo-tmp") != std::string::npos) tmp = true;
        if (n.rfind ("_was_2", 0) == 0 && n != "_was_20200101-0000_keep.wav") { ++was; const std::string c = slurp (e.path());
            oldAlpha = oldAlpha || c == "OLD ALPHA"; oldTwo = oldTwo || c == "OLD TWO"; oldSet = oldSet || c == "#CEO-SCENE 1\nOLD SETTINGS\n"; }
    }
    check (was == 3 && oldAlpha && oldTwo && oldSet, "the old WAVs and the old settings file were renamed, content intact", was);
    check (! tmp, "no temporary files left behind");
    const auto pr = sc::loadPresetFile ((d / "Ceo scene.txt").string());
    check (pr.valid && pr.ceoFile && pr.name == "My scene" && pr.s.inputMode == 1 && pr.s.modTarget == 0 && pr.s.pitchScale == 6, "Ceo scene.txt holds the settings");
    for (int l : { 0, 2, 4 })
    {
        const auto info = a.lib.info (l);
        check (info.bound && info.status == Lib::Ok && ! info.unsaved && fs::path (info.fullPath).parent_path() == d, "the layer is bound to its new file in the Scene folder");
    }
    check (a.lib.info (1).fullPath.empty(), "an empty layer has no file");

    // saving again keeps everything from the first time too
    size_t count1 = 0; for (auto& e : fs::directory_iterator (d)) { (void) e; ++count1; }
    check (a.lib.saveScene (d.string(), sc::writeCeoSettings ("again", s), why), "a second save is accepted once the first has finished");
    a.settle();
    check (a.lib.takeSceneSaved (r) && r.ok, "second save ok");
    size_t count2 = 0; for (auto& e : fs::directory_iterator (d)) { (void) e; ++count2; }
    check (count2 == count1 + 4, "the second save renamed the first save's four files and wrote four new ones (nothing lost)", (double) (count2 - count1));
    check (slurp (d / "MidSide.txt") == before, "Arbhar's file is still untouched");

    // the saved Scene loads back
    const auto f = sc::scanFolder (d.string());
    check (f.byDigit && f.audioCount == 3 && ! f.audio[0].empty() && ! f.audio[2].empty() && ! f.audio[4].empty(), "scanning the saved folder finds exactly the three layers", f.audioCount);
    check (fs::path (f.settingsFile).filename() == "Ceo scene.txt", "and our settings file");
    Inst b; b.lib.setHomeFolder (home.string());
    std::vector<std::pair<int, std::string>> list; for (int i = 0; i < 6; ++i) if (! f.audio[i].empty()) list.push_back ({ i, f.audio[i] });
    check (b.lib.loadMany (list, why), "load back accepted");
    b.settle();
    check (toneOf (b.tail (0), 48000.0) == 1 && toneOf (b.tail (2), 48000.0) == 3 && toneOf (b.tail (4), 48000.0) == 5 && b.e.layerState (1) == Engine::LayerEmpty, "the same sounds came back in the same layers");

    // refusals and failures
    Inst e; e.lib.setHomeFolder (home.string());
    check (! e.lib.saveScene (d.string(), "x", why) && why.find ("nothing to save") != std::string::npos, "nothing to save: refused");
    check (! e.lib.saveScene ("", "x", why), "no folder: refused");
    e.record (1, kTone[2]);
    spitS (home / "iamafile", "x");
    check (e.lib.saveScene ((home / "iamafile" / "scene").string(), "x", why), "a folder that can't be made: accepted at first");
    e.settle();
    check (e.lib.takeSceneSaved (r) && ! r.ok && r.message.find ("Nothing was changed") != std::string::npos, "...then reported as failed, with nothing changed");
    check (! e.lib.info (1).bound && e.lib.info (1).unsaved, "the layer stays unsaved after a failed Scene save");
    e.p.layer = 0; e.p.capture = true; e.run (0.3, kTone[1]);
    check (! e.lib.saveScene (d.string(), "x", why) && why.find ("recording") != std::string::npos, "refused while recording");
    e.p.capture = false; e.run (0.3, 0.0);
    // an empty-named new folder is created
    e.settle();
    check (e.lib.saveScene ((home / "new" / "deeper" / "scene").string(), "x", why), "a new folder tree is accepted");
    e.settle();
    check (e.lib.takeSceneSaved (r) && r.ok && fs::exists (home / "new" / "deeper" / "scene" / "2_beta.wav"), "and created");
    fs::remove_all (home);
}

static void spitWav (const fs::path& p, double rate, double seconds, double hz, int channels = 2, float amp = 0.5f)
{
    const auto bytes = ceo::writeWav16 (makeTone (rate, seconds, hz, amp, channels));
    fs::create_directories (p.parent_path());
    std::ofstream f (p, std::ios::binary | std::ios::trunc);
    f.write ((const char*) bytes.data(), (std::streamsize) bytes.size());
}

static std::vector<float> slice (const std::vector<float>& v, double from, double to, double rate)
{
    const size_t a = std::min (v.size(), (size_t) (from * rate)), b = std::min (v.size(), (size_t) (to * rate));
    return std::vector<float> (v.begin() + (long) a, v.begin() + (long) b);
}

static void testJoin()
{
    std::printf ("[7] joining short files into one layer\n");
    const double R = 48000.0;
    auto partsOf = [&] (std::initializer_list<double> secs, double hz0 = 440.0)
    {
        std::vector<LayerAudio> v; double hz = hz0;
        for (double sec : secs) { v.push_back (makeTone (R, sec, hz, 0.5f, 2)); hz += 220.0; }
        return v;
    };
    LayerAudio out; ceo::JoinResult jr;

    // the 10 s rule: stop after the file that makes the total MORE than 10 s
    auto p4 = partsOf ({ 3.2, 2.5, 4.1, 2.0, 1.5 });
    check (ceo::joinLayerAudio (p4, 0.0, out, jr) && jr.used == 4 && std::abs (jr.seconds - 11.8) < 0.001 && ! jr.cut, "no gap: 3.2+2.5+4.1 = 9.8 s is not past 10 s, so the 4th file is taken too (11.8 s)", (double) jr.used, jr.seconds);
    check (ceo::joinLayerAudio (p4, 0.25, out, jr) && jr.used == 3 && std::abs (jr.seconds - 10.3) < 0.001, "0.25 s gap: 10.3 s after three files, so the 4th is left out", (double) jr.used, jr.seconds);
    check (ceo::joinLayerAudio (p4, 0.5, out, jr) && jr.used == 3 && std::abs (jr.seconds - 10.8) < 0.001, "0.5 s gap: 10.8 s after three files", (double) jr.used, jr.seconds);
    check (std::abs (ceo::joinedSeconds ({ p4[0], p4[1] }, 0.25) - 5.95) < 1e-6, "joinedSeconds counts the gap between files");
    check (std::abs (jr.seconds - (double) out.frames() / R) < 1e-9 && out.channels == 2 && out.sampleRate == R, "result: length, stereo, first file's rate");

    // exactly 10 s is not "more than 10 s"
    auto pe = partsOf ({ 5.0, 5.0, 1.0 });
    check (ceo::joinLayerAudio (pe, 0.0, out, jr) && jr.used == 3, "exactly 10.000 s: the next file is still taken", (double) jr.used);

    // one file comes back unchanged
    auto p1 = partsOf ({ 4.0, 2.0 });
    check (ceo::joinLayerAudio ({ p1[0] }, 0.25, out, jr) && jr.used == 1 && out.ch[0] == p1[0].ch[0] && out.ch[1] == p1[0].ch[1], "a single file is untouched (no fade, no cut)");
    LayerAudio longOne = makeTone (R, 14.0, 300.0, 0.5f, 2);
    check (ceo::joinLayerAudio ({ longOne }, 0.25, out, jr) && out.frames() == longOne.frames() && ! jr.cut, "a single long file is left to the engine (it cuts at 13 s itself)");

    // cut at 13 s
    auto pc = partsOf ({ 9.9, 6.0 });
    check (ceo::joinLayerAudio (pc, 0.25, out, jr) && jr.used == 2 && jr.cut && out.frames() == (size_t) (13.0 * R), "more than 13 s is cut at 13 s", (double) out.frames(), 13.0 * R);

    // what the joined audio is made of
    auto p3 = partsOf ({ 3.2, 2.5, 4.1 }, 440.0);
    ceo::joinLayerAudio (p3, 0.25, out, jr);
    const double g = 0.25;
    check (out.ch[0][0] == p3[0].ch[0][0] && out.ch[0][(size_t) (1.5 * R)] == p3[0].ch[0][(size_t) (1.5 * R)], "the start and middle of the first file are exactly as in the file");
    check (std::abs (out.ch[0][(size_t) (3.2 * R) - 1]) < 0.01f, "the end of the first file is faded (no click at the join)");
    {
        const auto gapSeg = slice (out.ch[0], 3.2 + 0.01, 3.2 + g - 0.01, R);
        bool zero = ! gapSeg.empty(); for (float v : gapSeg) if (v != 0.0f) zero = false;
        check (zero, "the gap is silence");
        const size_t second = (size_t) std::llround ((3.2 + g) * R);
        check (std::abs (out.ch[0][second]) < 0.01f, "the second file fades in");
        check (out.ch[0][second + (size_t) (1.0 * R)] == p3[1].ch[0][(size_t) (1.0 * R)], "the middle of the second file is exactly as in the file");
        check (out.ch[0].back() == p3[2].ch[0].back(), "the last file is not faded at its end");
    }
    check (amplitudeAt (slice (out.ch[0], 0.5, 3.0, R), 440.0, R) > 0.45 && amplitudeAt (slice (out.ch[0], 3.2 + g + 0.5, 3.2 + g + 2.0, R), 660.0, R) > 0.45
          && amplitudeAt (slice (out.ch[0], 3.2 + g + 2.5 + g + 0.5, 3.2 + g + 2.5 + g + 3.5, R), 880.0, R) > 0.45, "the three tones sit one after another in order");

    // mono and stereo, other sample rates
    std::vector<LayerAudio> mix;
    mix.push_back (makeTone (R, 2.0, 440.0, 0.5f, 1));
    mix.push_back (makeTone (R, 2.0, 660.0, 0.5f, 2, 880.0));
    check (ceo::joinLayerAudio (mix, 0.25, out, jr) && out.channels == 2 && out.ch[0].size() == out.ch[1].size(), "mono + stereo gives stereo");
    check (slice (out.ch[0], 0.2, 1.8, R) == slice (out.ch[1], 0.2, 1.8, R), "the mono file is on both sides");
    check (amplitudeAt (slice (out.ch[0], 2.25 + 0.3, 3.9, R), 660.0, R) > 0.45 && amplitudeAt (slice (out.ch[1], 2.25 + 0.3, 3.9, R), 880.0, R) > 0.45, "the stereo file keeps its two sides");
    std::vector<LayerAudio> mono2; mono2.push_back (makeTone (R, 1.0, 440.0, 0.5f, 1)); mono2.push_back (makeTone (R, 1.0, 550.0, 0.5f, 1));
    check (ceo::joinLayerAudio (mono2, 0.0, out, jr) && out.channels == 1 && out.ch[1].empty(), "two mono files stay mono");
    std::vector<LayerAudio> rates; rates.push_back (makeTone (R, 2.0, 440.0)); rates.push_back (makeTone (44100.0, 2.0, 660.0)); rates.push_back (makeTone (96000.0, 1.0, 880.0));
    check (ceo::joinLayerAudio (rates, 0.25, out, jr) && out.sampleRate == R && std::abs ((double) out.frames() / R - 5.5) < 0.002, "files at 44.1 and 96 kHz are brought to the first file's rate (length in seconds kept)", (double) out.frames() / R);
    check (amplitudeAt (slice (out.ch[0], 2.25 + 0.3, 4.1, R), 660.0, R) > 0.4 && amplitudeAt (slice (out.ch[0], 4.25 + 0.3, 5.2, R), 880.0, R) > 0.4, "and their pitch is right");
    check (! ceo::joinLayerAudio ({}, 0.25, out, jr), "nothing to join: false");

    // folder scan keeps the extra files of a layer
    const fs::path home = freshHome ("join");
    const fs::path d = home / "Scenes" / "drums";
    for (auto* n : { "1_kick.wav", "1_snare.wav", "1_hat.wav", "2_pad.wav", "3_b2.wav", "3_b1.wav", "4_x.txt" }) spitS (d / n, "x");
    auto f = sc::scanFolder (d.string());
    check (f.byDigit && f.audioCount == 3 && f.extraCount == 3, "scan: three layers, three extra files", (double) f.extraCount);
    check (fs::path (f.audio[0]).filename() == "1_hat.wav" && f.extra[0].size() == 2 && fs::path (f.extra[0][0]).filename() == "1_kick.wav" && fs::path (f.extra[0][1]).filename() == "1_snare.wav", "layer alpha: files in alphabetical order");
    check (fs::path (f.audio[2]).filename() == "3_b1.wav" && f.extra[2].size() == 1 && f.extra[1].empty(), "layer gamma: two files, beta: one");
    const fs::path d2 = home / "Scenes" / "plain";
    for (auto* n : { "a.wav", "b.wav", "c.wav" }) spitS (d2 / n, "x");
    f = sc::scanFolder (d2.string());
    check (! f.byDigit && f.extraCount == 0, "without digits there is nothing to join (the first six files fill the layers)");

    // loading the layer from real files
    const fs::path jd = home / "Scenes" / "join_scene";
    spitWav (jd / "1_a.wav", R, 3.2, 440.0); spitWav (jd / "1_b.wav", R, 2.5, 660.0); spitWav (jd / "1_c.wav", R, 4.1, 880.0, 1);
    spitS (jd / "1_d.wav", "this file is not a wav, but it is past 10 s so it is never read");
    spitWav (jd / "2_one.wav", R, 3.0, 1100.0);
    const std::vector<std::string> a3 = { (jd / "1_a.wav").string(), (jd / "1_b.wav").string(), (jd / "1_c.wav").string(), (jd / "1_d.wav").string() };
    Inst a; a.lib.setHomeFolder (home.string());
    std::string why; std::vector<Lib::JoinInfo> infos;
    check (a.lib.loadGroups ({ { 0, a3 }, { 1, { (jd / "2_one.wav").string() } } }, 0.25, why, &infos), "loadGroups accepted (the bad 4th file is never read)");
    a.settle();
    check (infos.size() == 2 && infos[0].layer == 0 && infos[0].used == 3 && infos[0].leftOut == 1 && std::abs (infos[0].seconds - 10.3) < 0.002 && ! infos[0].cut, "report: alpha 3 files joined, 1 left out, 10.3 s", (double) infos[0].used, infos[0].seconds);
    check (infos[1].used == 1 && infos[1].leftOut == 0, "report: beta is a single file");
    {
        const auto w = a.whole (0);
        check (a.e.layerState (0) == Engine::LayerFilled && amplitudeAt (slice (w, 0.5, 3.0, R), 440.0, R) > 0.4 && amplitudeAt (slice (w, 3.45 + 0.5, 3.45 + 2.0, R), 660.0, R) > 0.4
              && amplitudeAt (slice (w, 5.95 + 0.5, 5.95 + 3.5, R), 880.0, R) > 0.4, "the layer holds the three tones in order");
        check (toneOf (a.tail (1), R) == 0 || a.e.layerState (1) == Engine::LayerFilled, "layer beta holds its single file");
    }
    const auto info0 = a.lib.info (0);
    check (info0.bound && info0.files == 3 && info0.fileName == "1_a.wav" && info0.status == Lib::Ok, "alpha is linked to its first file and knows it is joined from 3");
    check (a.lib.info (1).files == 1, "beta: one file");
    auto binds = a.lib.bindings();
    check (binds.size() == 2 && binds[0].second.more.size() == 2 && binds[0].second.gapMs == 250 && binds[1].second.more.empty() && binds[1].second.gapMs == 0, "bindings: two further files and the gap for alpha");
    check (! a.lib.info (0).unsaved, "a joined layer is not 'unsaved'");

    // a bad file among the ones that are used: nothing at all is loaded
    Inst b; b.lib.setHomeFolder (home.string());
    spitS (jd / "1_b_bad.wav", "not a wav");
    check (! b.lib.loadGroups ({ { 0, { a3[0], (jd / "1_b_bad.wav").string() } } }, 0.25, why) && why.find ("1_b_bad.wav") != std::string::npos && why.find ("Nothing was loaded") != std::string::npos, "a bad file that is needed: refused, naming it");
    b.settle();
    check (b.e.layerState (0) == Engine::LayerEmpty && ! b.lib.info (0).bound, "and nothing was loaded");
    fs::remove (jd / "1_b_bad.wav");

    // a project brings the joined layer back (same audio, sample for sample)
    {
        Inst c; c.lib.setHomeFolder (home.string());
        c.lib.restore (binds);
        c.settle();
        check (c.e.layerState (0) == Engine::LayerFilled && c.whole (0) == a.whole (0) && c.whole (1) == a.whole (1), "restore: the same audio comes back");
        check (c.lib.info (0).files == 3 && c.lib.info (0).status == Lib::Ok && sc::scanFolder (jd.string()).audioCount == 2, "restore: still linked to three files");
        // asking again with the same binding keeps everything (the host set the same state twice)
        c.lib.restore (c.lib.bindings()); c.settle();
        check (c.lib.info (0).status == Lib::Ok && c.e.layerState (0) == Engine::LayerFilled, "restoring the same state twice changes nothing");
        // moving a middle file inside the home folder: found again by its fingerprint
        fs::create_directories (home / "elsewhere");
        fs::rename (jd / "1_b.wav", home / "elsewhere" / "renamed_b.wav");
        Inst e2; e2.lib.setHomeFolder (home.string()); e2.lib.restore (binds); e2.settle();
        check (e2.e.layerState (0) == Engine::LayerFilled && e2.whole (0) == a.whole (0) && e2.lib.info (0).status == Lib::Ok, "a moved and renamed middle file is found by size and fingerprint");
        // a missing file: red 'missing', naming it; the layer stays empty
        fs::remove (home / "elsewhere" / "renamed_b.wav");
        Inst m; m.lib.setHomeFolder (home.string()); m.lib.restore (binds); m.settle();
        const auto mi = m.lib.info (0);
        check (mi.status == Lib::Missing && mi.message.find ("1_b.wav") != std::string::npos && m.e.layerState (0) == Engine::LayerEmpty, "a missing file: Missing, names the file, layer stays empty");
        spitWav (jd / "1_b.wav", R, 2.5, 660.0);       // put back (same bytes)
        // a changed file loads but is flagged
        spitWav (jd / "1_c.wav", R, 4.1, 990.0, 1);
        Inst ch; ch.lib.setHomeFolder (home.string()); ch.lib.restore (binds); ch.settle();
        check (ch.lib.info (0).status == Lib::Changed && ch.e.layerState (0) == Engine::LayerFilled, "a changed file: loaded and flagged as changed");
        spitWav (jd / "1_c.wav", R, 4.1, 880.0, 1);
    }

    // a sample-rate change reloads the joined layer from its files
    {
        a.sr = 44100.0; a.e.prepare (44100.0, 441, 2); a.run (0.3, 0.0); a.settle();
        const auto w = a.whole (0);
        check (a.e.layerState (0) == Engine::LayerFilled && a.lib.info (0).files == 3, "sample rate changed: the joined layer came back from its three files", (double) a.lib.info (0).files);
        check (amplitudeAt (slice (w, 0.5, 3.0, 44100.0), 440.0, 44100.0) > 0.4 && amplitudeAt (slice (w, 3.45 + 0.5, 3.45 + 2.0, 44100.0), 660.0, 44100.0) > 0.4, "with the tones in the right places at the new rate");
    }
    // recording over the layer drops all the links
    {
        Inst r; r.lib.setHomeFolder (home.string()); r.lib.restore (binds); r.settle();
        r.record (0, kTone[2], 0.8);
        check (! r.lib.info (0).bound && r.lib.info (0).unsaved && r.lib.bindings().empty() == false, "recording over a joined layer drops its links (beta stays linked)");
        r.settle();
    }
    fs::remove_all (home);
}

static void testSourceHygiene()
{
    std::printf ("[8] the scene code has no plugin framework in it\n");
    for (const char* path : { CEO_SCENE_H, CEO_SCENE_CPP })
    {
        const std::string text = slurp (path);
        check (! text.empty(), "source found");
        checkS (text.find ("juce") == std::string::npos && text.find ("JUCE") == std::string::npos, "no JUCE in the scene code", path);
    }
    for (const char* path : { CEO_ENGINE_CPP })
    {
        const std::string text = slurp (path);
        checkS (text.find ("Scene") == std::string::npos && text.find ("filesystem") == std::string::npos, "the audio engine knows nothing about Scenes or files", path);
    }
}

int main()
{
    testParser();
    testOfficialStyle();
    testScale();
    testScan();
    testDecide();
    testLoadMany();
    testSaveScene();
    testJoin();
    testSourceHygiene();
    std::printf ("\nscene_test: %d checks, %d failed\n", gChecks, gFailed);
    return gFailed == 0 ? 0 : 1;
}
