#include "PluginProcessor.h"

CeoAudioProcessor::CeoAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", CeoParams::createLayout())
{
}

void CeoAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, getTotalNumInputChannels());
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
    p.layer          = (int) (get (CeoParams::layer) + 0.5f);
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
}

juce::AudioProcessorEditor* CeoAudioProcessor::createEditor()
{
    // Milestone 2: Logic/JUCE's plain generic sliders. The styled panel is milestone 6.
    return new juce::GenericAudioProcessorEditor (*this);
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
