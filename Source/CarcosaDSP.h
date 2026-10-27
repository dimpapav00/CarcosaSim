#pragma once
// Carcosa Sim - DSP core.
//
// Behavioural model of a two-stage silicon fuzz, built from the public description of the DOD Carcosa
// (controls BEFORE, AFTER = bias, OUTPUT, HI-CUT and a DEMHE/HALI bass switch). The real circuit is not
// published, so the internals below are educated guesses, not a schematic-exact copy:
//
//   input -> DEMHE (adds bass / low mids) or HALI (cuts bass) -> stage 1: gain set by BEFORE (asymmetric, bandwidth
//   falls with gain) -> stage 2: starved-bias stage set by AFTER (gating, sag, splatter at high settings)
//   -> mid/treble emphasis -> HI-CUT low-pass -> OUTPUT level

#include <juce_dsp/juce_dsp.h>
#include "NoiseGate.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace carcosa
{
constexpr float kTwoPi = 6.283185307179586f;

struct Params
{
    float before   = 0.5f;   // 0..1  fuzz amount (stage 1 gain)
    float after    = 0.3f;   // 0..1  bias of stage 2: 0 = loud and woody, 1 = starved, gated and splattery
    float output   = 0.5f;   // 0..1  level (0.5 ~ unity-ish, 1 = about +12 dB)
    float hiCut    = 0.3f;   // 0..1  0 = open, 1 = dark
    float inputDb  = 0.0f;   // host-side trim before the gate
    float trimDb   = 0.0f;   // host-side output trim
    float gateDb   = -80.0f; // input noise gate threshold, <= -79.9 means off
    bool  on       = true;
    bool  demhe    = true;   // true = DEMHE (adds bass, for clean amps), false = HALI (cuts bass, for dirty amps)
};

struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void setCutoff (float fc, float fs) { a = 1.0f - std::exp (-kTwoPi * fc / fs); }
    float lp (float x) { z += a * (x - z); return z; }
    float hp (float x) { return x - lp (x); }
    void reset() { z = 0.0f; }
};

struct Coeffs
{
    float onMix = 1, modeMix = 1, g1 = 1, fc1a = 0.5f, bias = 0, neg = 1, sag = 0, rest = 0, level = 1, hiA = 0.5f;
};

class Channel
{
public:
    void prepare (float fsOS)
    {
        dcIn.setCutoff (15.0f, fsOS);
        bassLow.setCutoff (220.0f, fsOS);
        haliHP.setCutoff (300.0f, fsOS);
        coupling1.setCutoff (35.0f, fsOS);
        coupling2.setCutoff (20.0f, fsOS);
        presLo.setCutoff (1000.0f, fsOS);
        presHi.setCutoff (6000.0f, fsOS);
        envLP.setCutoff (10.0f, fsOS);
        stage1Rest = std::tanh (0.12f);
        reset();
    }

    void reset()
    {
        for (auto* f : { &dcIn, &bassLow, &haliHP, &miller, &coupling1, &coupling2, &presLo, &presHi, &hi1, &hi2, &envLP })
            f->reset();
        env = 0.0f;
    }

    float process (float x, const Coeffs& c)
    {
        const float v = dcIn.hp (x);

        // DEMHE: bass and low-mid boost.  HALI: bass cut.
        const float demhe = v + 0.9f * bassLow.lp (v);
        const float hali = haliHP.hp (v) * 1.1f;
        float y = hali + c.modeMix * (demhe - hali);

        // stage 1: gain from BEFORE, bandwidth falls as the gain rises (Miller effect), slight asymmetry
        miller.a = c.fc1a;
        y = miller.lp (y * c.g1);
        y = std::tanh (y + 0.12f) - stage1Rest;
        y = coupling1.hp (y);

        // stage 2: AFTER starves the bias. The coupling capacitor charges with the signal (envelope), so loud
        // passages shift the bias further: the stage sags and gates on decays instead of sustaining.
        const float beff = c.bias + c.sag * env;
        const float u = y - beff;
        const float g2 = 3.0f;
        float z;
        if (u >= 0.0f)
            z = std::tanh (g2 * u);
        else
            z = -c.neg * std::tanh (g2 * (-u) / c.neg);
        z -= c.rest;                       // idle level of the stage (removed so nothing thumps when AFTER moves)
        env = envLP.lp (std::fabs (z));
        z = coupling2.hp (z);

        // mid / treble emphasis ("velcro"), then HI-CUT (two poles)
        z += 0.4f * (presHi.lp (z) - presLo.lp (z));
        hi1.a = hi2.a = c.hiA;
        z = hi2.lp (hi1.lp (z));

        const float out = v + c.onMix * (z * c.level - v);
        if (! std::isfinite (out))
        {
            reset();
            return 0.0f;
        }
        return std::clamp (out, -8.0f, 8.0f);
    }

private:
    OnePole dcIn, bassLow, haliHP, miller, coupling1, coupling2, presLo, presHi, hi1, hi2, envLP;
    float env = 0.0f, stage1Rest = 0.119427f;
};

class Engine
{
public:
    static constexpr int kChunk = 32;

    void prepare (double sampleRate)
    {
        fsOS = (float) sampleRate * 4.0f;
        os.reset();
        os.initProcessing ((size_t) kChunk);
        gate.prepare (sampleRate);
        for (auto& c : channels)
            c.prepare (fsOS);
        for (auto* s : { &smBefore, &smAfter, &smOut, &smHi, &smMode, &smOn, &smTrim })
            s->reset (sampleRate, 0.03);
        smIn.reset (sampleRate, 0.03);
        snap = true;
    }

    void reset()
    {
        os.reset();
        gate.reset();
        for (auto& c : channels) c.reset();
        snap = true;
    }

    int getLatencySamples() const { return (int) std::lround (os.getLatencyInSamples()) + gate.getLatencySamples(); }

    void process (juce::AudioBuffer<float>& buffer, const Params& p)
    {
        const int numCh = std::min (buffer.getNumChannels(), 2);
        const int n = buffer.getNumSamples();
        if (numCh == 0 || n == 0) return;

        auto set = [this] (juce::SmoothedValue<float>& s, float v)
        {
            if (snap) s.setCurrentAndTargetValue (v); else s.setTargetValue (v);
        };
        set (smBefore, p.before); set (smAfter, p.after); set (smOut, p.output); set (smHi, p.hiCut);
        set (smMode, p.demhe ? 1.0f : 0.0f);
        set (smOn, p.on ? 1.0f : 0.0f);
        set (smIn, p.inputDb); set (smTrim, p.trimDb);
        snap = false;

        // input trim, then the gate (the threshold is measured after the trim; bypassed pedal = no gate)
        for (int i = 0; i < n; ++i)
        {
            const float g = std::pow (10.0f, smIn.getNextValue() / 20.0f);
            for (int ch = 0; ch < numCh; ++ch)
                buffer.getWritePointer (ch)[i] *= g;
        }
        gate.process (buffer, numCh, p.on ? p.gateDb : NoiseGate::kOffDb);

        for (int pos = 0; pos < n; pos += kChunk)
        {
            const int len = std::min (kChunk, n - pos);

            Coeffs c;
            const float before = smBefore.skip (len), after = smAfter.skip (len);
            const float out = smOut.skip (len), hi = smHi.skip (len);
            c.onMix = smOn.skip (len);
            c.modeMix = smMode.skip (len);

            c.g1 = std::pow (10.0f, before * 42.0f / 20.0f);                 // 0 .. +42 dB
            const float fc1 = std::clamp (300.0e3f / c.g1, 3000.0f, 0.45f * fsOS);
            c.fc1a = 1.0f - std::exp (-kTwoPi * fc1 / fsOS);
            c.bias = after * 0.55f;
            c.neg = 1.0f - 0.6f * after;
            c.sag = after * 0.35f;
            c.rest = -c.neg * std::tanh (3.0f * c.bias / c.neg);   // stage 2 output at rest (no signal, no sag)
            const float lv = out * 2.0f;
            c.level = lv * lv * 0.5f * std::pow (10.0f, smTrim.skip (len) / 20.0f);
            const float fcHi = std::min (14000.0f * std::pow (1000.0f / 14000.0f, hi), 0.45f * fsOS);
            c.hiA = 1.0f - std::exp (-kTwoPi * fcHi / fsOS);

            juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) numCh, (size_t) pos, (size_t) len);
            auto up = os.processSamplesUp (block);
            for (int ch = 0; ch < numCh; ++ch)
            {
                float* d = up.getChannelPointer ((size_t) ch);
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                    d[i] = channels[(size_t) ch].process (d[i], c);
            }
            os.processSamplesDown (block);
        }
    }

private:
    float fsOS = 176400.0f;
    bool snap = true;
    std::array<Channel, 2> channels;
    NoiseGate gate;
    juce::dsp::Oversampling<float> os { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    juce::SmoothedValue<float> smBefore, smAfter, smOut, smHi, smMode, smOn, smTrim, smIn;
};
} // namespace carcosa
