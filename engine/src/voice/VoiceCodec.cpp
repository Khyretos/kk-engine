#include "kke/voice/VoiceCodec.h"

#include <opus.h>

#include <algorithm>

namespace kke::voice {

VoiceEncoder::VoiceEncoder() {
    int err = OPUS_OK;
    m_enc = opus_encoder_create(kSampleRate, 1, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK) {
        m_error = opus_strerror(err);
        m_enc = nullptr;
        return;
    }
    opus_encoder_ctl(m_enc, OPUS_SET_BITRATE(24000));
    opus_encoder_ctl(m_enc, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(m_enc, OPUS_SET_PACKET_LOSS_PERC(10));
    opus_encoder_ctl(m_enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
}

VoiceEncoder::~VoiceEncoder() {
    if (m_enc) opus_encoder_destroy(m_enc);
}

void VoiceEncoder::setBitrate(int bps) {
    if (m_enc) opus_encoder_ctl(m_enc, OPUS_SET_BITRATE(std::clamp(bps, 6000, 96000)));
}

void VoiceEncoder::setExpectedLoss(int percent) {
    if (m_enc) opus_encoder_ctl(m_enc, OPUS_SET_PACKET_LOSS_PERC(std::clamp(percent, 0, 100)));
}

std::vector<uint8_t> VoiceEncoder::encode(const float* frame) {
    if (!m_enc) return {};
    uint8_t buf[512];
    const opus_int32 n = opus_encode_float(m_enc, frame, kFrameSamples, buf, 256); // kMaxVoiceBytes
    if (n <= 0) return {};
    return { buf, buf + n };
}

VoiceDecoder::VoiceDecoder() {
    int err = OPUS_OK;
    m_dec = opus_decoder_create(kSampleRate, 1, &err);
    if (err != OPUS_OK) m_dec = nullptr;
}

VoiceDecoder::~VoiceDecoder() {
    if (m_dec) opus_decoder_destroy(m_dec);
}

bool VoiceDecoder::decode(const std::vector<uint8_t>& frame, float* out) {
    const int n = m_dec && !frame.empty() ? opus_decode_float(m_dec, frame.data(), static_cast<opus_int32>(frame.size()), out, kFrameSamples, 0) : -1;
    if (n == kFrameSamples) return true;
    std::fill(out, out + kFrameSamples, 0.0f);
    return false;
}

bool VoiceDecoder::conceal(const std::vector<uint8_t>* next, float* out) {
    int n = -1;
    if (m_dec && next && !next->empty()) n = opus_decode_float(m_dec, next->data(), static_cast<opus_int32>(next->size()), out, kFrameSamples, 1);
    if (n != kFrameSamples && m_dec) n = opus_decode_float(m_dec, nullptr, 0, out, kFrameSamples, 0);
    if (n == kFrameSamples) return true;
    std::fill(out, out + kFrameSamples, 0.0f);
    return false;
}

} // namespace kke::voice
