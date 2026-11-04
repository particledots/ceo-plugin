#include "PluginProcessor.h"
#include "PluginEditor.h"

CeoAudioProcessor::CeoAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", CeoParams::createLayout())
{
}

CeoAudioProcessor::~CeoAudioProcessor()
{
    cancelPendingUpdate();
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
        copyXmlToBinary (*xml, destData);
}

void CeoAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CeoAudioProcessor();
}
