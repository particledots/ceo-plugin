#include "ceo/Engine.h"

namespace ceo
{

void Engine::prepare (double newSampleRate, int newMaxBlockSize, int numChannels)
{
    sampleRate   = newSampleRate;
    maxBlockSize = newMaxBlockSize;
    channelCount = numChannels;
    reset();
}

void Engine::reset()
{
    // Nothing to clear yet. Layers and grains will be reset here from milestone 3.
}

void Engine::setParams (const Params& newParams)
{
    params = newParams;
}

void Engine::process (float* const* channels, int numChannels, int numSamples)
{
    // Milestone 2: pass-through. The audio is already in the buffer, so doing
    // nothing leaves it exactly as it arrived.
    (void) channels;
    (void) numChannels;
    (void) numSamples;
}

} // namespace ceo
