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
                    const int layer = e->getIntAttribute ("index") - 1;
                    if (layer >= 0 && layer < ceo::kNumLayers && ! b.path.empty())
                        refs.push_back ({ layer, b });
                }
                xml->removeChildElement (node, true);
            }
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            library.restore (refs);
        }
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CeoAudioProcessor();
}
