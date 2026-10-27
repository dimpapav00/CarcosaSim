// Offline sanity test for the Carcosa Sim DSP core (no GUI needed).
#include "CarcosaDSP.h"
#include <cstdio>

static double goertzel (const std::vector<float>& x, double f, double fs)
{
    const double w = 2.0 * M_PI * f / fs;
    double s1 = 0, s2 = 0, c = 2.0 * std::cos (w);
    for (float v : x) { double s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
    const double p = s1 * s1 + s2 * s2 - c * s1 * s2;
    return 2.0 * std::sqrt (std::max (0.0, p)) / (double) x.size();
}

struct Result { double rms, peak, dc, h1, h2, h3; bool finite; std::vector<float> tail; };

static Result run (carcosa::Params p, double amp, double f0 = 330.0, double fs = 48000.0, double seconds = 1.0)
{
    carcosa::Engine eng;
    eng.prepare (fs);
    const int N = (int) (fs * seconds), block = 512;
    juce::AudioBuffer<float> buf (2, block);
    std::vector<float> out;
    for (int pos = 0; pos < N; pos += block)
    {
        for (int i = 0; i < block; ++i)
        {
            float v = (float) (amp * std::sin (2.0 * M_PI * f0 * (pos + i) / fs));
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        eng.process (buf, p);
        for (int i = 0; i < block; ++i) out.push_back (buf.getSample (0, i));
    }
    Result r { 0, 0, 0, 0, 0, 0, true, { out.begin() + out.size() / 2, out.end() } };
    for (float v : r.tail)
    {
        if (! std::isfinite (v)) r.finite = false;
        r.rms += v * v; r.peak = std::max (r.peak, (double) std::fabs (v)); r.dc += v;
    }
    r.rms = std::sqrt (r.rms / r.tail.size()); r.dc /= r.tail.size();
    r.h1 = goertzel (r.tail, f0, fs); r.h2 = goertzel (r.tail, 2 * f0, fs); r.h3 = goertzel (r.tail, 3 * f0, fs);
    return r;
}

static void show (const char* name, const Result& r)
{
    std::printf ("%-40s rms=%.3f peak=%.3f dc=%+.4f  f0=%.3f 2f0=%.3f 3f0=%.3f  %s\n", name, r.rms, r.peak, r.dc,
                 r.h1, r.h2, r.h3, r.finite ? "ok" : "NaN/Inf!");
}
static double dbr (double a, double b) { return 20.0 * std::log10 (a / b); }

int main()
{
    int failures = 0;
    auto check = [&failures] (bool cond, const char* msg) { if (! cond) { std::printf ("FAIL: %s\n", msg); ++failures; } };

    // --- bypass is transparent
    carcosa::Params off; off.on = false;
    auto rb = run (off, 0.1);
    show ("bypassed (on = false)", rb);
    check (std::fabs (rb.rms - 0.1 / std::sqrt (2.0)) < 0.004, "bypass passes the signal at unity gain");

    // --- defaults
    carcosa::Params def;
    auto rd = run (def, 0.1);
    show ("defaults, -20 dBFS in", rd);
    check (rd.finite, "finite output with defaults");
    check (rd.rms > 0.05 && rd.rms < 1.5, "default level is usable");
    check (rd.h3 > 0.01, "odd harmonics present");
    check (std::fabs (rd.dc) < 0.01, "no DC offset");

    // --- AFTER = 0 symmetric, AFTER = 1 asymmetric (even harmonics rise)
    carcosa::Params a0 = def, a1 = def; a0.after = 0.0f; a1.after = 1.0f;
    auto ra0 = run (a0, 0.1), ra1 = run (a1, 0.1);
    show ("AFTER 0 (woody)", ra0);
    show ("AFTER 1 (starved)", ra1);
    check (ra0.h2 < 0.05 * ra0.h1, "AFTER 0: nearly symmetric, even harmonics small");
    check (ra1.h2 > 3.0 * ra0.h2 + 0.002, "AFTER 1: even harmonics grow (broken, splattery)");

    // --- gating: with AFTER high a quiet signal is attenuated much more than a loud one (guitar volume cleans it up differently)
    carcosa::Params g0 = def, g1 = def; g0.after = 0.0f; g1.after = 1.0f; g0.before = g1.before = 0.4f;
    const double qa = 0.004, la = 0.1;
    const double gainQuiet0 = run (g0, qa).rms / (qa / std::sqrt (2.0)), gainLoud0 = run (g0, la).rms / (la / std::sqrt (2.0));
    const double gainQuiet1 = run (g1, qa).rms / (qa / std::sqrt (2.0)), gainLoud1 = run (g1, la).rms / (la / std::sqrt (2.0));
    std::printf ("gain quiet/loud: AFTER 0 -> %.1f/%.1f   AFTER 1 -> %.1f/%.1f\n", gainQuiet0, gainLoud0, gainQuiet1, gainLoud1);
    check (gainQuiet1 / gainLoud1 < 0.6 * (gainQuiet0 / gainLoud0), "AFTER high: quiet notes lose gain relative to loud ones (gating)");

    // --- BEFORE adds gain
    carcosa::Params b0 = def, b1 = def; b0.before = 0.1f; b1.before = 0.9f;
    auto rbl = run (b0, 0.01), rbh = run (b1, 0.01);
    show ("BEFORE 0.1 (-40 dBFS in)", rbl);
    show ("BEFORE 0.9 (-40 dBFS in)", rbh);
    check (rbh.rms > 3.0 * rbl.rms, "BEFORE raises the level of a quiet signal");
    check (rbh.rms > 0.05, "BEFORE high gives a strong fuzz from a quiet signal");

    // --- OUTPUT is a level control, and with BEFORE/AFTER at zero the pedal acts as a boost
    carcosa::Params o1 = def, o2 = def; o1.output = 0.3f; o2.output = 0.9f;
    check (run (o2, 0.1).rms > 4.0 * run (o1, 0.1).rms, "OUTPUT controls the level");
    carcosa::Params boost = def; boost.before = 0.0f; boost.after = 0.0f; boost.output = 0.8f;
    auto rboost = run (boost, 0.02);
    show ("boost use (BEFORE 0, AFTER 0, OUT 0.8)", rboost);
    check (rboost.rms > 0.02 / std::sqrt (2.0) * 1.5, "BEFORE and AFTER at zero: usable clean boost");

    // --- DEMHE adds bass relative to HALI, HI-CUT darkens
    carcosa::Params dem = def, hali = def; dem.demhe = true; hali.demhe = false; dem.before = hali.before = 0.1f;
    const double bass = dbr (run (dem, 0.005, 90.0).rms, run (hali, 0.005, 90.0).rms);
    const double mid = dbr (run (dem, 0.005, 1500.0).rms, run (hali, 0.005, 1500.0).rms);
    std::printf ("DEMHE vs HALI: 90 Hz %+.1f dB, 1.5 kHz %+.1f dB\n", bass, mid);
    check (bass > 6.0, "DEMHE has clearly more bass than HALI");
    check (bass > mid + 4.0, "the DEMHE/HALI difference is mostly in the bass");
    carcosa::Params bright = def, dark = def; bright.hiCut = 0.0f; dark.hiCut = 1.0f; bright.before = dark.before = 0.1f;
    const double cut = dbr (run (dark, 0.005, 5000.0).rms, run (bright, 0.005, 5000.0).rms);
    std::printf ("HI-CUT 1 vs 0: 5 kHz %+.1f dB\n", cut);
    check (cut < -12.0, "HI-CUT turned up removes the treble");

    // --- stability at extremes
    bool allFinite = true; double worstDc = 0, maxPeak = 0;
    for (int demhe = 0; demhe < 2; ++demhe)
        for (float be : { 0.0f, 1.0f })
            for (float af : { 0.0f, 0.5f, 1.0f })
                for (float hc : { 0.0f, 1.0f })
                {
                    carcosa::Params e; e.demhe = demhe; e.before = be; e.after = af; e.hiCut = hc; e.output = 1.0f; e.inputDb = 24.0f; e.trimDb = 12.0f;
                    for (double a : { 1.0, 0.001 })
                    {
                        auto r = run (e, a, 110.0);
                        allFinite = allFinite && r.finite;
                        worstDc = std::max (worstDc, std::fabs (r.dc) / std::max (r.peak, 1.0e-3));
                        maxPeak = std::max (maxPeak, r.peak);
                    }
                }
    std::printf ("extremes: finite=%d worst |dc|/peak=%.4f max peak=%.2f\n", allFinite, worstDc, maxPeak);
    check (allFinite, "all extreme settings stay finite");
    check (maxPeak <= 8.0 * 1.3, "output stays within the clamp plus oversampler overshoot");
    check (worstDc < 0.03, "DC stays below 3% of the peak even at extreme settings");

    // --- steady state is periodic (375 Hz at 48 kHz repeats every 128 samples): no unwanted noise from the model
    {
        auto r = run (def, 0.05, 375.0, 48000.0, 2.0);
        double err = 0, en = 0;
        for (size_t i = 0; i + 128 < r.tail.size(); ++i) { const double d = r.tail[i + 128] - r.tail[i]; err += d * d; en += (double) r.tail[i] * r.tail[i]; }
        std::printf ("periodicity error: %.2e relative\n", std::sqrt (err / en));
        check (std::sqrt (err / en) < 1.0e-3, "steady state is periodic (no model noise)");
    }

    carcosa::Engine e; e.prepare (48000.0);
    std::printf ("reported latency: %d samples\n", e.getLatencySamples());
    check (e.getLatencySamples() > 0, "latency is reported");

    std::printf (failures ? "\n%d FAILURES\n" : "\nALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
