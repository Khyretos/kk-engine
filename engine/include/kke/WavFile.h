#pragma once

#include <cstddef>
#include <string>

namespace kke {

// Writes interleaved float samples (-1..1, clipped) as a 16-bit PCM WAV.
// miniaudio is built without its encoders (MA_NO_ENCODING), and a WAV
// header is 44 bytes. False, with the reason in `error`, when the file
// can't be written or the arguments make no sense.
bool writeWav(const std::string& path, const float* interleaved, size_t sampleCount, int channels, int sampleRate,
              std::string* error = nullptr);

} // namespace kke
