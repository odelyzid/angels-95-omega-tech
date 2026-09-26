#pragma once
#include "raylib.h"
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// DspReverb — Schroeder reverberator
//
// 4 comb filters in parallel → 2 all-pass filters in series.
// Applies reverb to audio sample buffers in-place.
//
// Attach once after InitAudioDevice():
//   DspReverb::Reset();
//   AttachAudioMixedProcessor(DspReverb::AudioCallback);
//
// Usage (safe from any thread):
//   DspReverb::SetMix(0.4f);       // wet/dry blend 0..1 (0 = dry passthrough)
//   DspReverb::SetDecay(0.6f);     // reverb tail length 0..1
// ---------------------------------------------------------------------------

class DspReverb {
public:
    // --- Parameters ---
    static void SetMix(float mix) {
        float v = (mix < 0.0f) ? 0.0f : (mix > 1.0f) ? 1.0f : mix;
        s_mix.store(v, std::memory_order_relaxed);
    }
    static void SetDecay(float decay) {
        float d = (decay < 0.0f) ? 0.0f : (decay > 1.0f) ? 1.0f : decay;
        s_decay.store(d, std::memory_order_relaxed);
        // Recompute comb feedback so the decay parameter has audible effect.
        // Keep below 1.0 for stability; position pointers are untouched.
        if (s_initialized.load(std::memory_order_acquire)) {
            float fb = 0.55f + d * 0.38f;
            for (int c = 0; c < 4; c++) s_combFeed[c].store(fb, std::memory_order_relaxed);
        }
    }
    static float Mix() { return s_mix.load(std::memory_order_relaxed); }
    static float Decay() { return s_decay.load(std::memory_order_relaxed); }

    // --- Process audio buffer in-place ---
    static void Process(float* samples, int frameCount, int channels) {
        if (!s_initialized.load(std::memory_order_acquire)) Init();
        if (channels <= 0) channels = 1;

        float mix = s_mix.load(std::memory_order_relaxed);
        for (int f = 0; f < frameCount; f++) {
            for (int ch = 0; ch < channels && ch < 2; ch++) {
                int idx = f * channels + ch;
                float dry = samples[idx];

                // Comb filters (parallel)
                float wet = 0.0f;
                for (int c = 0; c < 4; c++) {
                    float* buf = s_combBuf[c].data();
                    int& pos = s_combPos[c];
                    int len = s_combLen[c];
                    float fb = s_combFeed[c].load(std::memory_order_relaxed);
                    float out = buf[pos];
                    buf[pos] = dry + out * fb;
                    wet += out;
                    pos = (pos + 1) % len;
                }
                wet *= 0.25f;  // average 4 combs

                // All-pass filters (series)
                for (int a = 0; a < 2; a++) {
                    float* buf = s_allpassBuf[a].data();
                    int& pos = s_allpassPos[a];
                    int len = s_allpassLen[a];
                    float fb = s_allpassFeed[a].load(std::memory_order_relaxed);
                    float inp = wet;
                    float out = buf[pos];
                    wet = -inp * fb + out;
                    buf[pos] = inp + out * fb;
                    pos = (pos + 1) % len;
                }

                samples[idx] = dry * (1.0f - mix) + wet * mix;
            }
        }
    }

    // --- Reset internal delay buffers ---
    static void Reset() {
        if (!s_initialized.load(std::memory_order_acquire)) Init();
        for (int c = 0; c < 4; c++)
            std::memset(s_combBuf[c].data(), 0, s_combLen[c] * sizeof(float));
        for (int a = 0; a < 2; a++)
            std::memset(s_allpassBuf[a].data(), 0, s_allpassLen[a] * sizeof(float));
    }

    // --- raylib audio callback adapter ---
    static void AudioCallback(void* buffer, unsigned int frames) {
        float* samples = (float*)buffer;
        // raylib provides interleaved stereo float samples [-1..1]
        Process(samples, (int)frames, 2);
    }

private:
    static void Init() {
        // Comb filter delays (in samples at 44100 Hz)
        s_combLen[0] = 1116; s_combLen[1] = 1188;
        s_combLen[2] = 1277; s_combLen[3] = 1356;
        // Feedback derived from decay (0..1 → 0.55..0.93) so tail length is tunable
        float fb = 0.55f + s_decay.load(std::memory_order_relaxed) * 0.38f;
        for (int c = 0; c < 4; c++) {
            s_combFeed[c].store(fb, std::memory_order_relaxed);
            s_combPos[c] = 0;
        }
        for (int c = 0; c < 4; c++)
            s_combBuf[c].resize(s_combLen[c], 0.0f);

        // All-pass filter delays
        s_allpassLen[0] = 556; s_allpassLen[1] = 441;
        s_allpassFeed[0].store(0.5f, std::memory_order_relaxed);
        s_allpassFeed[1].store(0.5f, std::memory_order_relaxed);
        s_allpassPos[0] = 0;
        s_allpassPos[1] = 0;
        for (int a = 0; a < 2; a++)
            s_allpassBuf[a].resize(s_allpassLen[a], 0.0f);

        s_initialized.store(true, std::memory_order_release);
    }

    static std::atomic<bool> s_initialized;
    static std::atomic<float> s_mix;
    static std::atomic<float> s_decay;

    // Comb filter state
    static std::vector<float> s_combBuf[4];
    static int s_combLen[4];
    static int s_combPos[4];
    static std::atomic<float> s_combFeed[4];

    // All-pass filter state
    static std::vector<float> s_allpassBuf[2];
    static int s_allpassLen[2];
    static int s_allpassPos[2];
    static std::atomic<float> s_allpassFeed[2];
};

// Static definitions
std::atomic<bool> DspReverb::s_initialized = false;
std::atomic<float> DspReverb::s_mix = 0.0f;   // pure dry until a reverb zone is entered
std::atomic<float> DspReverb::s_decay = 0.5f;
std::vector<float> DspReverb::s_combBuf[4];
int DspReverb::s_combLen[4];
int DspReverb::s_combPos[4];
std::atomic<float> DspReverb::s_combFeed[4];
std::vector<float> DspReverb::s_allpassBuf[2];
int DspReverb::s_allpassLen[2];
int DspReverb::s_allpassPos[2];
std::atomic<float> DspReverb::s_allpassFeed[2];
