#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Remembered per computer (not per project): the folder where saved layers go and where moved files are searched for.
    std::unique_ptr<juce::PropertiesFile> openSettings()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Unofficial Ceo";
        o.folderName = "particledots";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        return std::make_unique<juce::PropertiesFile> (o);
    }

    juce::String readSetting (const char* key)
    {
        if (auto s = openSettings()) return s->getValue (key);
        return {};
    }
    void writeSetting (const char* key, const juce::String& value)
    {
        if (auto s = openSettings()) { s->setValue (key, value); s->saveIfNeeded(); }
    }

    const char* const kLayerWords[ceo::kNumLayers] = { "alpha", "beta", "gamma", "delta", "epsilon", "zeta" };

    juce::File loadHomeFolder()
    {
        if (auto s = openSettings())
        {
            const auto v = s->getValue ("homeFolder");
            if (v.isNotEmpty())
                return juce::File (v);
        }
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Unofficial Ceo");
    }
}

CeoAudioProcessor::CeoAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", CeoParams::createLayout())
{
    homeFolder = loadHomeFolder();
    library.setHomeFolder (homeFolder.getFullPathName().toStdString());
    {
        const auto sf = readSetting ("scenesFolder");
        scenesFolder = sf.isNotEmpty() ? juce::File (sf)
                                       : juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Unofficial Ceo").getChildFile ("Scenes");
        loadWhat = juce::jlimit (0, 3, readSetting ("loadWhat").getIntValue());
        joinShort = readSetting ("joinShort").getIntValue() == 1;
    }
    startTimerHz (10);
}

CeoAudioProcessor::~CeoAudioProcessor()
{
    stopTimer();
    cancelPendingUpdate();
}

void CeoAudioProcessor::setHomeFolder (const juce::File& f)
{
    homeFolder = f;
    if (auto s = openSettings())
    {
        s->setValue ("homeFolder", f.getFullPathName());
        s->saveIfNeeded();
    }
    library.setHomeFolder (f.getFullPathName().toStdString());
}

void CeoAudioProcessor::timerCallback()
{
    library.tick();

    ceo::LayerLibrary::SceneSaved done;
    if (library.takeSceneSaved (done))
    {
        juce::String layers;
        for (int l : done.layers) layers << juce::String (l + 1);
        if (done.ok)
            setSceneLink (juce::File (juce::String::fromUTF8 (done.folder.c_str())), layers);
        const juce::ScopedLock sl (sceneLock);
        sceneMessage = juce::String::fromUTF8 (done.message.c_str());
    }
}

//==============================================================================
// Milestone 11: Scenes
void CeoAudioProcessor::setScenesFolder (const juce::File& f)
{
    scenesFolder = f;
    writeSetting ("scenesFolder", f.getFullPathName());
}

void CeoAudioProcessor::setLoadWhat (int w)
{
    loadWhat = juce::jlimit (0, 3, w);
    writeSetting ("loadWhat", juce::String (loadWhat));
}

void CeoAudioProcessor::setJoinShort (bool on)
{
    joinShort = on;
    writeSetting ("joinShort", on ? "1" : "0");
}

void CeoAudioProcessor::setSceneLink (const juce::File& folder, const juce::String& mask)
{
    const juce::ScopedLock sl (sceneLock);
    sceneFolder = folder;
    sceneMask = mask;
}

juce::String CeoAudioProcessor::takeSceneMessage()
{
    const juce::ScopedLock sl (sceneLock);
    auto m = sceneMessage;
    sceneMessage.clear();
    return m;
}

juce::String CeoAudioProcessor::getSceneReport() const
{
    const juce::ScopedLock sl (sceneLock);
    return sceneReport;
}

CeoAudioProcessor::SceneStatus CeoAudioProcessor::getSceneStatus() const
{
    juce::File folder; juce::String mask;
    {
        const juce::ScopedLock sl (sceneLock);
        folder = sceneFolder; mask = sceneMask;
    }
    SceneStatus s;
    if (folder == juce::File())
        return s;
    s.name = folder.getFileName();
    if (! folder.isDirectory()) { s.state = 3; return s; }
    s.state = 1;
    for (int i = 0; i < ceo::kNumLayers; ++i)
    {
        if (! mask.containsChar ((juce::juce_wchar) ('1' + i)))
            continue;
        const auto info = library.info (i);
        if (info.status == ceo::LayerLibrary::Waiting || info.status == ceo::LayerLibrary::Working)
            continue;                                   // still loading or saving: not a change
        if (! info.bound || juce::File (juce::String::fromUTF8 (info.fullPath.c_str())).getParentDirectory() != folder)
            s.state = 2;
    }
    return s;
}

ceo::scene::Settings CeoAudioProcessor::currentSettings() const
{
    auto idx = [this] (const char* id) { return (int) (apvts.getRawParameterValue (id)->load() + 0.5f); };
    ceo::scene::Settings s;
    s.inputMode = idx (CeoParams::inputMode);
    s.modTarget = idx (CeoParams::modTarget);
    s.follow = idx (CeoParams::follow);
    s.onsetCapture = idx (CeoParams::onsetCapture);
    s.pitchSnap = idx (CeoParams::pitchSnap);
    s.pitchScale = idx (CeoParams::pitchScale);
    return s;
}

void CeoAudioProcessor::applySettings (const ceo::scene::Settings& s)
{
    auto set = [this] (const char* id, int index)
    {
        if (index < 0) return;
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
            p->endChangeGesture();
        }
    };
    set (CeoParams::inputMode, s.inputMode);
    set (CeoParams::modTarget, s.modTarget);
    set (CeoParams::follow, s.follow);
    set (CeoParams::onsetCapture, s.onsetCapture);
    set (CeoParams::pitchSnap, s.pitchSnap);
    set (CeoParams::pitchScale, s.pitchScale);
}

CeoAudioProcessor::ScenePlan CeoAudioProcessor::planSceneLoad (const juce::File& folder)
{
    ScenePlan p;
    p.folder = folder;
    if (! folder.isDirectory()) { p.message = "That Scene folder isn't there."; return p; }
    p.files = ceo::scene::scanFolder (folder.getFullPathName().toStdString());
    if (! p.files.settingsFile.empty())
        p.preset = ceo::scene::loadPresetFile (p.files.settingsFile);
    p.what = (ceo::scene::LoadWhat) loadWhat;
    p.decision = ceo::scene::decide (p.what, p.files, p.preset);
    if (! p.decision.layers && ! p.decision.settings)
    {
        p.message = folder.getFileName() + ": " + juce::String::fromUTF8 ((p.decision.note.empty() ? std::string ("Nothing to load.") : p.decision.note).c_str());
        return p;
    }
    p.ok = true;
    if (p.decision.layers)
        for (int i = 0; i < ceo::kNumLayers; ++i)
            if (! p.files.audio[i].empty() && library.info (i).unsaved)
                p.unsavedLayers.add (kLayerWords[i]);
    return p;
}

CeoAudioProcessor::SceneResult CeoAudioProcessor::runSceneLoad (const ScenePlan& plan)
{
    SceneResult r;
    const auto name = plan.folder.getFileName();
    juce::String mask;
    int count = 0;
    juce::String report = "Scene: " + name + "\n";

    juce::StringArray joinedBrief;
    if (plan.decision.layers)
    {
        std::vector<ceo::LayerLibrary::LoadItem> items;
        for (int i = 0; i < ceo::kNumLayers; ++i)
            if (! plan.files.audio[i].empty())
            {
                ceo::LayerLibrary::LoadItem it; it.layer = i;
                it.files.push_back (plan.files.audio[i]);
                if (joinShort)
                    for (const auto& more : plan.files.extra[i]) it.files.push_back (more);
                items.push_back (std::move (it));
                mask << juce::String (i + 1); ++count;
            }
        std::string why;
        std::vector<ceo::LayerLibrary::JoinInfo> infos;
        if (! library.loadGroups (items, ceo::kJoinGapSeconds, why, &infos))
        {
            r.message = juce::String::fromUTF8 (why.c_str());
            return r;                                   // nothing was loaded, nothing was changed
        }
        auto nameOf = [] (const std::string& f) { return juce::File (juce::String::fromUTF8 (f.c_str())).getFileName(); };
        report << "\nLayers loaded (" << juce::String (count) << "):\n";
        for (size_t k = 0; k < items.size(); ++k)
        {
            const auto& it = items[k];
            const auto& ji = infos[k];
            report << "  " << kLayerWords[it.layer] << ": ";
            if (ji.used <= 1 && ji.leftOut == 0)
            {
                report << nameOf (it.files[0]) << "\n";
                continue;
            }
            if (ji.used > 1)
            {
                report << juce::String (ji.used) << " files joined (" << juce::String (ji.seconds, 1) << " s, "
                       << juce::String (ceo::kJoinGapSeconds, 2) << " s gap): ";
                for (int f = 0; f < ji.used; ++f) report << nameOf (it.files[(size_t) f]) << (f + 1 < ji.used ? ", " : "");
                joinedBrief.add (juce::String (kLayerWords[it.layer]) + " " + juce::String (ji.used) + " joined");
                if (ji.cut) report << "; the end was cut at 13 s";
            }
            else
                report << nameOf (it.files[0]);
            if (ji.leftOut > 0)
            {
                report << "; not used, the layer was already past 10 s: ";
                for (size_t f = (size_t) ji.used; f < it.files.size(); ++f) report << nameOf (it.files[f]) << (f + 1 < it.files.size() ? ", " : "");
            }
            report << "\n";
        }
        if (! joinShort && plan.files.extraCount > 0)
            report << "\nJoin short files is off: " << juce::String (plan.files.extraCount) << (plan.files.extraCount == 1 ? " more file" : " more files")
                   << " with a layer digit in front were not loaded (only the first file of each layer is). Switch it on in the Load menu to join them.\n";
    }
    else
        report << "\nLayers: not loaded.\n";

    juce::String shortMsg;
    if (plan.decision.layers)
        shortMsg << juce::String (count) << (count == 1 ? " layer" : " layers") << " loaded"
                 << (joinedBrief.isEmpty() ? juce::String() : " (" + joinedBrief.joinIntoString (", ") + ")") << ". ";

    const bool usable = plan.preset.valid && plan.preset.s.any();
    if (plan.decision.settings)
    {
        applySettings (plan.preset.s);
        report << "\nSettings from " << juce::File (juce::String::fromUTF8 (plan.files.settingsFile.c_str())).getFileName()
               << (plan.preset.name.empty() ? juce::String() : " (\"" + juce::String::fromUTF8 (plan.preset.name.c_str()) + "\")") << ":\n";
        juce::StringArray brief;
        for (const auto& line : plan.preset.set)
        {
            const auto t = juce::String::fromUTF8 (line.c_str());
            report << "  Set: " << t << "\n";
            brief.add (t.upToFirstOccurrenceOf (" (", false, false).replace (": ", " ").replace ("Input mode ", "Input ").replace ("Onset capture ", "Onset ")
                                                                                .replace ("Follow mode ", "Follow ").replace ("Pitch Scale ", "Scale ").replace ("Pitch Snap ", "Snap "));
        }
        shortMsg << "Set: " << brief.joinIntoString (", ");
        const int ignored = (int) plan.preset.ignored.size();
        if (ignored > 0)
        {
            shortMsg << ". " << juce::String (ignored) << " ignored.";
            report << "\nIgnored (Ceo has no setting for these, " << juce::String (ignored) << "):\n  ";
            for (size_t i = 0; i < plan.preset.ignored.size(); ++i)
                report << juce::String::fromUTF8 (plan.preset.ignored[i].c_str()) << (i + 1 < plan.preset.ignored.size() ? ", " : "");
            report << "\n";
        }
        for (const auto& n : plan.preset.notes)
            report << "\nNote: " << juce::String::fromUTF8 (n.c_str()) << "\n";
        const int effInput = plan.preset.s.inputMode >= 0 ? plan.preset.s.inputMode : currentSettings().inputMode;
        if (plan.preset.s.onsetCapture == 1 && effInput == 1)
            report << "\nNote: Onset capture is on, but it only works with the Mono input mode (Input mode is Stereo).\n";
    }
    else if (usable || ! plan.files.settingsFile.empty())
    {
        const juce::String why = plan.what == ceo::scene::LayersOnly ? "Load is set to Layers only"
                               : (plan.what == ceo::scene::AsFileSays && plan.preset.loadConfiguration == 2) ? "the file says layers only"
                               : "see the Load menu";
        shortMsg << "Settings not loaded (" << why << ").";
        report << "\nSettings: not loaded (" << why << ").\n";
    }

    setSceneLink (plan.folder, mask);
    {
        const juce::ScopedLock sl (sceneLock);
        sceneReport = report;
    }
    r.ok = true;
    r.message = shortMsg;
    return r;
}

bool CeoAudioProcessor::startSceneSave (const juce::File& folder, juce::String& whyNot)
{
    const auto settings = ceo::scene::writeCeoSettings (folder.getFileName().toStdString(), currentSettings());
    std::string why;
    if (! library.saveScene (folder.getFullPathName().toStdString(), settings, why))
    {
        whyNot = juce::String::fromUTF8 (why.c_str());
        return false;
    }
    return true;
}

void CeoAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, getTotalNumInputChannels());
    seenCaptureRanOut = engine.captureRanOutCount();
}

// A Capture recording stops by itself when the layer is full (13 s). The engine then reports it, and here the
// Capture button is switched off to match, so the next click starts a new recording.
void CeoAudioProcessor::handleAsyncUpdate()
{
    if (engine.isRecording())
        return;                                 // a new recording has already started: leave the button alone

    if (auto* capture = apvts.getParameter (CeoParams::capture))
    {
        if (capture->getValue() > 0.5f)
        {
            capture->beginChangeGesture();
            capture->setValueNotifyingHost (0.0f);
            capture->endChangeGesture();
        }
    }
}

bool CeoAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono in / mono out, or stereo in / stereo out.
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

ceo::Params CeoAudioProcessor::readParams() const
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    ceo::Params p;
    p.inputLevel     = get (CeoParams::inputLevel);
    p.dryWet         = get (CeoParams::dryWet);
    p.outputLevel    = get (CeoParams::outputLevel);
    p.capture        = get (CeoParams::capture) > 0.5f;
    p.strike         = get (CeoParams::strike) > 0.5f;
    p.erase          = get (CeoParams::erase) > 0.5f;
    p.undo           = get (CeoParams::undo) > 0.5f;
    p.follow         = get (CeoParams::follow) > 0.5f;
    p.layer          = (int) (get (CeoParams::layer) + 0.5f);
    p.stereoInput    = (int) (get (CeoParams::inputMode) + 0.5f) == 1;
    p.onsetCapture   = get (CeoParams::onsetCapture) > 0.5f;
    p.modTarget      = (int) (get (CeoParams::modTarget) + 0.5f);
    p.pitchSnap      = (int) (get (CeoParams::pitchSnap) + 0.5f);
    p.pitchScale     = (int) (get (CeoParams::pitchScale) + 0.5f);
    p.intensity      = get (CeoParams::intensity);
    p.grainWindow    = get (CeoParams::grainWindow);
    p.grainDirection = get (CeoParams::grainDirection);
    p.length         = get (CeoParams::length);
    p.scan           = get (CeoParams::scan);
    p.spray          = get (CeoParams::spray);
    p.pitch          = get (CeoParams::pitch);
    p.pitchDeviation = get (CeoParams::pitchDeviation);
    p.modAmount      = get (CeoParams::modAmount);
    p.hold           = get (CeoParams::hold);
    p.sense          = get (CeoParams::sense);
    p.dub            = get (CeoParams::dub);
    return p;
}

void CeoAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Silence any output channels that have no matching input (not expected with our layouts).
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    engine.setParams (readParams());
    engine.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());

    const uint32_t ranOut = engine.captureRanOutCount();
    if (ranOut != seenCaptureRanOut)
    {
        seenCaptureRanOut = ranOut;
        triggerAsyncUpdate();
    }
}

juce::AudioProcessorEditor* CeoAudioProcessor::createEditor()
{
    // The styled panel (milestone 6).
    return new CeoEditor (*this);
}

void CeoAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
    {
        // The layers' files (not their audio): which layer (1..6), where the file is (relative to the home folder when it
        // is inside it), its size and its fingerprint, so the file can be found again if it was moved.
        const auto refs = library.bindings();
        if (! refs.empty())
        {
            auto* node = xml->createNewChildElement ("LAYERFILES");
            for (const auto& [layer, b] : refs)
            {
                auto* e = node->createNewChildElement ("LAYER");
                e->setAttribute ("index", layer + 1);
                e->setAttribute ("path", juce::String::fromUTF8 (b.path.c_str()));
                e->setAttribute ("size", juce::String ((juce::int64) b.size));
                e->setAttribute ("hash", juce::String::toHexString ((juce::int64) b.hash));
                if (! b.more.empty())                                  // a layer joined from several files (milestone 12)
                {
                    e->setAttribute ("gap", b.gapMs);
                    for (const auto& pt : b.more)
                    {
                        auto* pe = e->createNewChildElement ("PART");
                        pe->setAttribute ("path", juce::String::fromUTF8 (pt.path.c_str()));
                        pe->setAttribute ("size", juce::String ((juce::int64) pt.size));
                        pe->setAttribute ("hash", juce::String::toHexString ((juce::int64) pt.hash));
                    }
                }
            }
        }
        {
            juce::File folder; juce::String mask;
            {
                const juce::ScopedLock sl (sceneLock);
                folder = sceneFolder; mask = sceneMask;
            }
            if (folder != juce::File())
            {
                auto* e = xml->createNewChildElement ("SCENE");
                e->setAttribute ("path", folder.isAChildOf (scenesFolder) ? folder.getRelativePathFrom (scenesFolder) : folder.getFullPathName());
                e->setAttribute ("layers", mask);
            }
        }
        copyXmlToBinary (*xml, destData);
    }
}

void CeoAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            std::vector<std::pair<int, ceo::LayerLibrary::Binding>> refs;
            if (auto* node = xml->getChildByName ("LAYERFILES"))     // absent in projects saved before milestone 10
            {
                for (auto* e : node->getChildIterator())
                {
                    ceo::LayerLibrary::Binding b;
                    b.path = e->getStringAttribute ("path").toStdString();
                    b.size = (uint64_t) e->getStringAttribute ("size").getLargeIntValue();
                    b.hash = (uint64_t) e->getStringAttribute ("hash").getHexValue64();
                    for (auto* pe : e->getChildWithTagNameIterator ("PART"))     // absent in projects saved before milestone 12
                    {
                        ceo::LayerLibrary::Binding::Part pt;
                        pt.path = pe->getStringAttribute ("path").toStdString();
                        pt.size = (uint64_t) pe->getStringAttribute ("size").getLargeIntValue();
                        pt.hash = (uint64_t) pe->getStringAttribute ("hash").getHexValue64();
                        if (! pt.path.empty()) b.more.push_back (pt);
                    }
                    b.gapMs = b.more.empty() ? 0 : e->getIntAttribute ("gap", 250);
                    const int layer = e->getIntAttribute ("index") - 1;
                    if (layer >= 0 && layer < ceo::kNumLayers && ! b.path.empty())
                        refs.push_back ({ layer, b });
                }
                xml->removeChildElement (node, true);
            }
            juce::File linked; juce::String linkedMask;
            if (auto* node = xml->getChildByName ("SCENE"))          // absent in projects saved before milestone 11
            {
                const auto path = node->getStringAttribute ("path");
                if (path.isNotEmpty())
                    linked = juce::File::isAbsolutePath (path) ? juce::File (path) : scenesFolder.getChildFile (path);
                linkedMask = node->getStringAttribute ("layers");
                xml->removeChildElement (node, true);
            }
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            library.restore (refs);
            setSceneLink (linked, linkedMask);                       // the settings are NOT applied again: the project has its own
        }
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CeoAudioProcessor();
}
