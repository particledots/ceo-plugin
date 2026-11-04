#include "ceo/Scene.h"
#include "ceo/Scales.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace ceo
{
namespace scene
{

namespace
{
    std::string lower (std::string s)
    {
        std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
        return s;
    }
    std::string trim (const std::string& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && (std::isspace ((unsigned char) s[a]) || s[a] == '\xef' || s[a] == '\xbb' || s[a] == '\xbf')) ++a;   // also a UTF-8 byte-order mark
        while (b > a && std::isspace ((unsigned char) s[b - 1])) --b;
        return s.substr (a, b - a);
    }
    bool startsWith (const std::string& s, const char* p) { return s.rfind (p, 0) == 0; }
    // letters and digits only, lower case: "LoadConf iguration" -> "loadconfiguration"
    std::string squash (const std::string& s)
    {
        std::string r;
        for (unsigned char c : s) if (std::isalnum (c)) r += (char) std::tolower (c);
        return r;
    }
    bool parseInt (const std::string& s, int& out)
    {
        const std::string t = trim (s);
        if (t.empty()) return false;
        char* end = nullptr;
        const long v = std::strtol (t.c_str(), &end, 10);
        if (end == t.c_str() || *end != '\0') return false;
        out = (int) v; return true;
    }
    std::string stripValue (std::string v)
    {
        const size_t c = v.find ("//");
        if (c != std::string::npos) v = v.substr (0, c);
        v = trim (v);
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = trim (v.substr (1, v.size() - 2));
        else if (! v.empty() && v.front() == '"') v = trim (v.substr (1));
        return v;
    }
    bool hidden (const std::string& name) { return name.empty() || name[0] == '.' || startsWith (name, "_was_"); }
    bool isWav (const fs::path& p) { return lower (p.extension().string()) == ".wav"; }
}

const char* inputModeName (int i)  { return i == 0 ? "Mono" : "Stereo"; }
const char* modTargetName (int i)
{
    static const char* n[] = { "Pan", "Hold", "Reverb", "Delay" };
    return n[std::max (0, std::min (3, i))];
}
const char* pitchSnapName (int i)
{
    static const char* n[] = { "Off", "Steps", "Semitones" };
    return n[std::max (0, std::min (2, i))];
}
std::string pitchScaleName (int i)
{
    if (i <= 0 || i > ceo::scales::kNumScales) return "Off";
    return ceo::scales::kScales[i - 1].name;
}

int nearestScale (const std::vector<int>& semitones)
{
    bool pc[12] = {}; pc[0] = true;
    for (int v : semitones) pc[((v % 12) + 12) % 12] = true;
    int have = 0; for (bool b : pc) have += b ? 1 : 0;
    if (have <= 1) return 0;                          // octaves only
    int best = 0; double bestScore = -1.0;
    for (int i = 0; i < ceo::scales::kNumScales; ++i)
    {
        bool sc[12] = {};
        const auto& s = ceo::scales::kScales[i];
        for (int k = 0; k < s.count; ++k) sc[s.notes[(size_t) k] % 12] = true;
        int both = 0, either = 0;
        for (int k = 0; k < 12; ++k) { both += (pc[k] && sc[k]) ? 1 : 0; either += (pc[k] || sc[k]) ? 1 : 0; }
        const double score = (double) both / (double) either;
        if (score > bestScore + 1e-9) { bestScore = score; best = i + 1; }
    }
    return best;
}

namespace
{
    // map a word (or number) to a choice using a list of {squashed word, value}
    struct Word { const char* w; int v; };
    int lookup (const std::string& value, const Word* list, int n)
    {
        int num;
        if (parseInt (value, num)) return num;       // numbers are handled by the caller; returned as is
        const std::string q = squash (value);
        for (int i = 0; i < n; ++i) if (q == list[i].w) return list[i].v;
        return -999;
    }

    int scaleFromWord (const std::string& value)
    {
        const std::string q = squash (value);
        if (q.empty()) return -1;
        if (q.find ("octave") != std::string::npos) return 0;
        if (q == "off" || q == "none" || q == "disable") return 0;
        if (q.find ("chromatic") != std::string::npos) return ceo::scales::kNumScales;
        if (q == "major" || q == "majorscale") return 1;
        if (q == "minor" || q == "naturalminor") return 6;
        for (int i = 0; i < ceo::scales::kNumScales; ++i)
        {
            const std::string n = squash (ceo::scales::kScales[i].name);
            if (n == q || n.find (q) != std::string::npos) return i + 1;
        }
        return -1;
    }
}

Preset parsePresetText (const std::string& text)
{
    Preset p;
    std::istringstream in (text);
    std::string line;
    bool first = true;
    std::set<std::string> ignoredSeen;
    while (std::getline (in, line))
    {
        const std::string t = trim (line);
        if (first)
        {
            if (t.empty()) continue;
            first = false;
            if (startsWith (t, "#ARB")) { p.valid = true; continue; }
            if (startsWith (t, "#CEO-SCENE")) { p.valid = true; p.ceoFile = true; continue; }
            return p;                                     // not a preset file
        }
        if (startsWith (t, "PRESET_NAME:"))
        {
            p.name = stripValue (t.substr (12));
            continue;
        }
        if (! startsWith (t, "PARAMETER:")) continue;     // comments, braces, stray comment closes, anything else
        const std::string rest = t.substr (10);
        const size_t colon = rest.find (':');
        if (colon == std::string::npos) continue;
        const std::string rawName = trim (rest.substr (0, colon));
        const std::string name = squash (rawName);
        const std::string value = stripValue (rest.substr (colon + 1));
        if (name.empty()) continue;

        int num = 0;
        const bool isNum = parseInt (value, num);
        const std::string q = squash (value);

        if (name == "loadconfiguration")
        {
            static const Word w[] = { { "loadpreset", 1 }, { "loadlayers", 2 }, { "loadscene", 3 }, { "disable", 0 }, { "none", 0 }, { "nothing", 0 } };
            const int v = lookup (value, w, 6);
            if (v >= 0 && v <= 3) p.loadConfiguration = v;
            else p.notes.push_back ("Load Configuration \"" + value + "\" was not understood");
        }
        else if (name == "inputmode")
        {
            static const Word w[] = { { "mono", 0 }, { "stereo", 1 } };
            const int v = lookup (value, w, 2);
            if (v == 0 || v == 1) { p.s.inputMode = v; p.set.push_back (std::string ("Input mode: ") + inputModeName (v)); }
            else p.notes.push_back ("Input mode \"" + value + "\" was not understood");
        }
        else if (name == "modcv" || name == "modtarget")
        {
            static const Word w[] = { { "panning", 1 }, { "pan", 1 }, { "stereopanning", 1 }, { "hold", 2 }, { "reverb", 3 }, { "delay", 4 }, { "none", 0 }, { "disable", 0 } };
            const int v = lookup (value, w, 8);
            if (v >= 1 && v <= 4) { p.s.modTarget = v - 1; p.set.push_back (std::string ("Mod target: ") + modTargetName (v - 1)); }
            else if (v == 0) p.notes.push_back ("Mod CV: none - Ceo always has a Mod target, so it is left as it is");
            else p.notes.push_back ("Mod CV \"" + value + "\" was not understood");
        }
        else if (name == "followmode" || name == "follow")
        {
            static const Word w[] = { { "scanmode", 0 }, { "scan", 0 }, { "followmode", 1 }, { "follow", 1 }, { "off", 0 }, { "on", 1 }, { "disable", 0 }, { "enable", 1 } };
            const int v = lookup (value, w, 8);
            if (v == 0 || v == 1) { p.s.follow = v; p.set.push_back (std::string ("Follow mode: ") + (v ? "on" : "off (Scan mode)")); }
            else p.notes.push_back ("Follow mode \"" + value + "\" was not understood");
        }
        else if (name == "onsetmode" || name == "onsetcapture")
        {
            int v = -1;
            if (isNum) v = (num == 0) ? 0 : (num == 1 || num == 2) ? 1 : -1;      // 1-2 = record on onset (alpha / beta)
            else if (q == "disable" || q == "off" || q == "none" || q == "disabled") v = 0;
            else if (q == "alpha" || q == "beta" || q == "enable" || q == "on") v = 1;
            if (v < 0) p.notes.push_back ("Onset mode \"" + value + "\" is not one Ceo has (only alpha / beta / off), so Onset capture is left as it is");
            if (v >= 0) { p.s.onsetCapture = v; p.set.push_back (std::string ("Onset capture: ") + (v ? "on" : "off")); }
        }
        else if (name == "quantisetable" || name == "pitchscale")
        {
            int sc = -1;
            std::vector<int> steps;
            std::istringstream ts (value);
            std::string tok;
            bool numeric = false, ok = true;
            while (ts >> tok)
            {
                for (char& c : tok) if (c == ',') c = ' ';
                std::istringstream sub (tok);
                std::string part;
                while (sub >> part) { int n; if (parseInt (part, n)) { steps.push_back (n); numeric = true; } else ok = false; }
            }
            if (numeric && ok && ! p.ceoFile) sc = nearestScale (steps);
            else if (numeric && p.ceoFile && steps.size() == 1) sc = std::max (0, std::min (ceo::scales::kNumScales, steps[0]));
            else sc = scaleFromWord (value);
            if (sc >= 0)
            {
                p.s.pitchScale = sc;
                if (p.ceoFile) p.set.push_back ("Pitch Scale: " + pitchScaleName (sc));
                else p.set.push_back ("Pitch Scale: " + pitchScaleName (sc) + " (the closest to the Arbhar's quantise table)");
            }
            else p.notes.push_back ("The quantise table was not understood");
        }
        else if (name == "pitchsnap" && p.ceoFile)
        {
            static const Word w[] = { { "off", 0 }, { "steps", 1 }, { "semitones", 2 } };
            const int v = lookup (value, w, 3);
            if (v >= 0 && v <= 2) { p.s.pitchSnap = v; p.set.push_back (std::string ("Pitch Snap: ") + pitchSnapName (v)); }
        }
        else
        {
            if (name != "loadconfiguration" && ignoredSeen.insert (name).second) p.ignored.push_back (rawName);
        }
    }
    return p;
}

Preset loadPresetFile (const std::string& file)
{
    std::ifstream f (file, std::ios::binary);
    if (! f) return Preset();
    f.seekg (0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n < 0 || n > 2 * 1024 * 1024) return Preset();
    f.seekg (0);
    std::string s ((size_t) n, '\0');
    if (n > 0) f.read (&s[0], n);
    return parsePresetText (s);
}

std::vector<std::string> listScenes (const std::string& scenesFolder)
{
    std::vector<std::string> out;
    std::error_code ec;
    if (scenesFolder.empty() || ! fs::is_directory (scenesFolder, ec)) return out;
    for (fs::directory_iterator it (scenesFolder, fs::directory_options::skip_permission_denied, ec), end; ! ec && it != end; it.increment (ec))
    {
        std::error_code e2;
        const std::string name = it->path().filename().string();
        if (it->is_directory (e2) && ! hidden (name)) out.push_back (it->path().string());
    }
    std::sort (out.begin(), out.end(), [] (const std::string& a, const std::string& b) { return lower (a) < lower (b); });
    return out;
}

SceneFiles scanFolder (const std::string& folder)
{
    SceneFiles r;
    r.folder = folder;
    std::error_code ec;
    r.name = fs::path (folder).filename().string();
    if (r.name.empty()) r.name = fs::path (folder).parent_path().filename().string();
    if (! fs::is_directory (folder, ec)) return r;

    std::vector<std::string> wavs, txts;
    for (fs::directory_iterator it (folder, fs::directory_options::skip_permission_denied, ec), end; ! ec && it != end; it.increment (ec))
    {
        std::error_code e2;
        if (! it->is_regular_file (e2)) continue;
        const std::string name = it->path().filename().string();
        if (hidden (name)) continue;
        if (isWav (it->path())) wavs.push_back (name);
        else if (lower (it->path().extension().string()) == ".txt") txts.push_back (name);
    }
    auto byName = [] (const std::string& a, const std::string& b) { const std::string x = lower (a), y = lower (b); return x != y ? x < y : a < b; };
    std::sort (wavs.begin(), wavs.end(), byName);
    std::sort (txts.begin(), txts.end(), byName);

    bool anyDigit = false;
    for (const auto& w : wavs) if (w[0] >= '1' && w[0] <= '6') { anyDigit = true; break; }
    r.byDigit = anyDigit;
    if (anyDigit)
    {
        for (const auto& w : wavs)
        {
            if (w[0] < '1' || w[0] > '6') continue;
            const int l = w[0] - '1';
            if (r.audio[l].empty()) r.audio[l] = (fs::path (folder) / w).string();
        }
    }
    else
    {
        for (size_t i = 0; i < wavs.size() && i < (size_t) kLayers; ++i) r.audio[i] = (fs::path (folder) / wavs[i]).string();
    }
    for (const auto& a : r.audio) if (! a.empty()) ++r.audioCount;

    // settings file: Ceo's own wins; else the first .txt that is an Arbhar preset
    std::string ceoOwn, arb;
    for (const auto& t : txts)
    {
        const std::string full = (fs::path (folder) / t).string();
        std::ifstream f (full, std::ios::binary);
        std::string head (64, '\0');
        if (f) f.read (&head[0], 64);
        head.resize ((size_t) f.gcount());
        const std::string h = trim (head);
        if (startsWith (h, "#CEO-SCENE")) { if (ceoOwn.empty()) ceoOwn = full; }
        else if (startsWith (h, "#ARB")) { if (arb.empty()) arb = full; }
    }
    r.settingsFile = ! ceoOwn.empty() ? ceoOwn : arb;
    return r;
}

Decision decide (LoadWhat what, const SceneFiles& f, const Preset& p)
{
    Decision d;
    const bool haveLayers = f.audioCount > 0;
    const bool haveSettings = p.valid && p.s.any();
    if (what == LayersOnly)
    {
        d.layers = haveLayers;
        if (! haveLayers) d.note = "There are no WAV files in that folder.";
    }
    else if (what == SettingsOnly)
    {
        d.settings = haveSettings;
        if (! haveSettings) d.note = f.settingsFile.empty() ? "There is no settings file in that folder." : "That settings file has nothing Ceo can use.";
    }
    else if (what == SceneAll)
    {
        d.layers = haveLayers; d.settings = haveSettings;
        if (! haveLayers && ! haveSettings) d.note = "Nothing to load: no WAV files and no usable settings in that folder.";
    }
    else   // AsFileSays
    {
        if (! haveLayers && ! haveSettings) { d.note = "Nothing to load: no WAV files and no usable settings in that folder."; return d; }
        if (f.settingsFile.empty() || ! p.valid) { d.layers = haveLayers; return d; }
        switch (p.loadConfiguration)
        {
            case 0:  d.note = "That file's Load Configuration says to load nothing. Pick Layers only, Scene or Settings only in the Load menu to override."; break;
            case 1:  d.settings = haveSettings; if (! haveSettings) d.note = "The file says settings only, but it has nothing Ceo can use."; break;
            case 2:  d.layers = haveLayers; if (! haveLayers) d.note = "The file says layers only, but the folder has no WAV files."; break;
            default: d.layers = haveLayers; d.settings = haveSettings; break;
        }
        if (! d.layers && ! d.settings && d.note.empty()) d.note = "Nothing to load.";
    }
    return d;
}

std::string writeCeoSettings (const std::string& name, const Settings& s)
{
    std::ostringstream o;
    o << "#CEO-SCENE 1\n";
    o << "// Written by the Unofficial Ceo plugin. Edit the values after the second colon if you like.\n";
    o << "PRESET_NAME: \"" << name << "\"\n";
    o << "PARAMETER: LoadConfiguration: 3\n";
    if (s.inputMode >= 0)    o << "PARAMETER: InputMode: " << inputModeName (s.inputMode) << "\n";
    if (s.modTarget >= 0)    o << "PARAMETER: ModCV: " << modTargetName (s.modTarget) << "\n";
    if (s.follow >= 0)       o << "PARAMETER: FollowMode: " << (s.follow ? "Follow" : "Scan") << "\n";
    if (s.onsetCapture >= 0) o << "PARAMETER: OnsetMode: " << (s.onsetCapture ? "Enable" : "Disable") << "\n";
    if (s.pitchSnap >= 0)    o << "PARAMETER: PitchSnap: " << pitchSnapName (s.pitchSnap) << "\n";
    if (s.pitchScale >= 0)   o << "PARAMETER: PitchScale: \"" << pitchScaleName (s.pitchScale) << "\"\n";
    return o.str();
}

} // namespace scene
} // namespace ceo
