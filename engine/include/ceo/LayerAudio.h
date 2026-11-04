// Plain audio container used to move a layer's audio in and out of the engine, and WAV reading / writing.
// No plugin or JUCE code here, so it is tested on its own with real files.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ceo
{

struct LayerAudio
{
    int channels = 0;            // 1 or 2 (a file with more channels keeps its first two)
    double sampleRate = 0.0;     // Hz
    std::vector<float> ch[2];    // one vector per channel, same length (-1..+1)
    size_t frames() const { return ch[0].size(); }
};

// 16-bit PCM WAV bytes for the audio (what SAVE WAV writes).
std::vector<uint8_t> writeWav16 (const LayerAudio& audio);

// Reads WAV bytes: 8/16/24/32-bit integer PCM or 32/64-bit float, any sample rate, mono or more channels.
// On failure returns false and sets 'error' to a sentence for the user.
bool readWav (const uint8_t* data, size_t size, LayerAudio& out, std::string& error);

// Converts audio from one sample rate to another (offline, whole signal at once). Same length in seconds.
std::vector<float> resampleOffline (const std::vector<float>& in, double inRate, double outRate);

} // namespace ceo
