#ifdef NDEBUG
#undef NDEBUG
#endif
#include <SkellCore/Core.h>
#include <SkellCore/backends/SkellyTuneBackend.h>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

int main() {
    constexpr double pi = 3.14159265358979323846;
    for (const int rate : {44100, 48000, 96000}) {
        skellcore::TempoClock clock;
        clock.prepare(rate);
        clock.update(123.0, 4.0, true);
        clock.advance(rate);
        assert(std::abs(clock.beatPosition() - 6.05) < 1e-9);
        assert(std::abs(clock.samplesPerBeat() - 60.0 * rate / 123.0) < 1.e-6);

        skellcore::FDNReverb reverb;
        reverb.prepare(rate);
        reverb.setParameters(1.8f, .3f, .35f);
        double tail = 0;
        for (int i = 0; i < rate; ++i) {
            const auto out = reverb.processFrame(i == 0 ? 0.7f : 0.0f, 0.f);
            assert(std::isfinite(out[0]) && std::isfinite(out[1]));
            tail += std::abs(out[0]) + std::abs(out[1]);
        }
        assert(tail > .1);

        skellcore::TruePeakLimiter limiter;
        limiter.prepare(rate, 2);
        limiter.setCeilingDb(-1);
        const int declaredLatency = limiter.latencySamples();
        assert(declaredLatency > 0);
        bool finiteOutput = true;
        double maxOutput = 0;
        for (int i = 0; i < rate; ++i) {
            const float input[2] {
                static_cast<float>(1.6 * std::sin(2 * pi * 997.0 * i / rate)),
                static_cast<float>(1.6 * std::sin(2 * pi * 1300.0 * i / rate))
            };
            float output[2]{};
            limiter.processFrame(input, output, 2);
            for (float v : output) {
                finiteOutput &= std::isfinite(v);
                maxOutput = std::max(maxOutput, std::abs(double(v)));
            }
        }
        assert(finiteOutput);
        assert(maxOutput <= .895); // Sampled peak guard only, NOT true-peak certification.

        skellcore::backends::SkellyTuneBackend tuning;
        skellcore::PitchFormantEngine pitch;
        pitch.setBackend(&tuning);
        pitch.prepare(rate, 2);
        assert(pitch.latencySamples() > 0);
        assert(pitch.latencySamples() < rate / 40);
        std::vector<float> a(512), b(512), wetA(512), wetB(512);
        for (int i = 0; i < 512; ++i) a[i] = b[i] = 0.18f * std::sin(2 * pi * 220 * i / rate);
        const float* input[2]{a.data(), b.data()};
        float* output[2]{wetA.data(), wetB.data()};
        pitch.processBlock(input,output,512,2);
        for (int i = 0; i < 512; ++i) {
            assert(std::isfinite(wetA[i]) && std::isfinite(wetB[i]));
        }
        pitch.reset();
        std::cout << rate << " Hz test passed\n";
    }
}
