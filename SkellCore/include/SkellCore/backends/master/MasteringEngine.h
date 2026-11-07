#pragma once

// SKELLY MASTER | portable DSP core (C++17, no GUI or host dependencies).
// State is allocated during prepare(); processFrame() performs no allocation.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace skellcore::master {
namespace detail {
constexpr double pi = 3.1415926535897932384626433832795;
inline double clamp(double v, double lo, double hi) noexcept { return std::max(lo, std::min(hi, v)); }
inline double dbToLinear(double db) noexcept { return std::pow(10.0, db / 20.0); }
inline double linearToDb(double v) noexcept { return 20.0 * std::log10(std::max(v, 1.0e-12)); }

class Smoother {
public:
    void prepare(double rate, double timeMs, double initial) noexcept {
        coefficient = 1.0 - std::exp(-1.0 / (std::max(1.0, rate) * std::max(0.001, timeMs * 0.001)));
        current = target = initial;
    }
    void setTarget(double newTarget) noexcept { target = newTarget; }
    double next() noexcept { current += (target - current) * coefficient; return current; }
    double value() const noexcept { return current; }
    void snap(double v) noexcept { current = target = v; }
private:
    double current = 0.0, target = 0.0, coefficient = 1.0;
};

// Transposed direct-form II, suitable for changing shelf coefficients without resetting state.
class Biquad {
public:
    void reset() noexcept { z1 = z2 = 0.0; }
    double process(double input) noexcept {
        const double output = b0 * input + z1;
        z1 = b1 * input - a1 * output + z2;
        z2 = b2 * input - a2 * output;
        // Suppress long-running subnormal tails without a hard audio-range gate.
        if (std::abs(z1) < 1.0e-28) z1 = 0.0;
        if (std::abs(z2) < 1.0e-28) z2 = 0.0;
        return output;
    }
    void setShelf(double sampleRate, double frequency, double gainDb, bool high) noexcept {
        const double A = std::pow(10.0, clamp(gainDb, -12.0, 12.0) / 40.0);
        const double w = 2.0 * pi * clamp(frequency, 20.0, sampleRate * 0.45) / sampleRate;
        const double c = std::cos(w), s = std::sin(w), rootA = std::sqrt(A);
        constexpr double slope = 0.85;
        const double alpha = s * 0.5 * std::sqrt((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
        double nb0, nb1, nb2, na0, na1, na2;
        if (high) {
            nb0 = A * ((A + 1) + (A - 1) * c + 2 * rootA * alpha);
            nb1 = -2 * A * ((A - 1) + (A + 1) * c);
            nb2 = A * ((A + 1) + (A - 1) * c - 2 * rootA * alpha);
            na0 = (A + 1) - (A - 1) * c + 2 * rootA * alpha;
            na1 = 2 * ((A - 1) - (A + 1) * c);
            na2 = (A + 1) - (A - 1) * c - 2 * rootA * alpha;
        } else {
            nb0 = A * ((A + 1) - (A - 1) * c + 2 * rootA * alpha);
            nb1 = 2 * A * ((A - 1) - (A + 1) * c);
            nb2 = A * ((A + 1) - (A - 1) * c - 2 * rootA * alpha);
            na0 = (A + 1) + (A - 1) * c + 2 * rootA * alpha;
            na1 = -2 * ((A - 1) + (A + 1) * c);
            na2 = (A + 1) + (A - 1) * c - 2 * rootA * alpha;
        }
        const double invA0 = 1.0 / na0;
        b0 = nb0 * invA0; b1 = nb1 * invA0; b2 = nb2 * invA0;
        a1 = na1 * invA0; a2 = na2 * invA0;
    }
private:
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;
};

class Compressor {
public:
    void prepare(double rate, double attackMs, double releaseMs) noexcept {
        attack = std::exp(-1.0 / (rate * attackMs * 0.001));
        release = std::exp(-1.0 / (rate * releaseMs * 0.001));
        gainAttack = std::exp(-1.0 / (rate * 0.003));
        gainRelease = std::exp(-1.0 / (rate * 0.070));
        reset();
    }
    void reset() noexcept { envelope = 0.0; gain = targetGain = 1.0; tick = 0; }
    double next(double stereoPeak, double amount, double thresholdAtMax, double ratioAtMax) noexcept {
        const double a = stereoPeak > envelope ? attack : release;
        envelope = a * envelope + (1.0 - a) * stereoPeak;
        if (++tick >= 16) {
            tick = 0;
            if (amount <= 0.00001) targetGain = 1.0;
            else {
                const double thresholdDb = -3.0 + (thresholdAtMax + 3.0) * amount;
                const double ratio = 1.0 + (ratioAtMax - 1.0) * amount;
                const double over = linearToDb(envelope) - thresholdDb;
                // 6dB soft knee, avoiding discontinuities at onset.
                double reductionDb = 0.0;
                if (over > 3.0) reductionDb = (1.0 - 1.0 / ratio) * over;
                else if (over > -3.0) {
                    const double x = over + 3.0;
                    reductionDb = (1.0 - 1.0 / ratio) * x * x / 12.0;
                }
                targetGain = dbToLinear(-reductionDb);
            }
        }
        const double g = targetGain < gain ? gainAttack : gainRelease;
        gain = g * gain + (1.0 - g) * targetGain;
        return gain;
    }
private:
    double envelope = 0.0, gain = 1.0, targetGain = 1.0;
    double attack = 0.0, release = 0.0, gainAttack = 0.0, gainRelease = 0.0;
    int tick = 0;
};

class OnePole {
public:
    void prepare(double rate, double hz) noexcept { a = std::exp(-2.0 * pi * hz / rate); z = 0.0; }
    void reset() noexcept { z = 0.0; }
    double low(double x) noexcept { z = a * z + (1.0 - a) * x; return z; }
private:
    double a = 0.0, z = 0.0;
};
} // namespace detail

struct MasterParameters {
    double inputDb = 0.0;
    double drive = 0.0;     // 0..1
    double glue = 0.0;      // 0..1
    double dynamics = 0.0;  // 0..1
    double lowsDb = 0.0;    // -6..+6
    double highsDb = 0.0;   // -6..+6
    double sauce = 0.0;     // 0..1
    double transients = 0.0;// 0..1
    double outputDb = 0.0;
    double ceilingDb = -1.0;
    bool bypass = false;
};

struct StereoFrame { float left = 0.0f, right = 0.0f; };

// Engine runs at 4x the host rate. Feed it frames from a quality oversampling filter.
class MasteringEngine {
public:
    void prepare(double oversampledRate, int numChannels, int lookaheadInOversampledSamples) {
        rate = std::max(8000.0, oversampledRate);
        channels = std::clamp(numChannels, 1, 2);
        lookahead = std::max(4, lookaheadInOversampledSamples);
        for (auto& v : programDelay) v.assign(static_cast<std::size_t>(lookahead), 0.0);
        for (auto& v : dryDelay) v.assign(static_cast<std::size_t>(lookahead), 0.0);
        input.prepare(rate, 15, 1); drive.prepare(rate, 25, 0);
        glueAmt.prepare(rate, 30, 0); dynamicsAmt.prepare(rate, 30, 0);
        lows.prepare(rate, 35, 0); highs.prepare(rate, 35, 0);
        sauceAmt.prepare(rate, 30, 0); transientsAmt.prepare(rate, 30, 0);
        output.prepare(rate, 15, 1); ceiling.prepare(rate, 15, detail::dbToLinear(-1));
        bypassMix.prepare(rate, 12, 0);
        glueComp.prepare(rate, 27, 220);
        dynamicsComp.prepare(rate, 8, 110);
        hf[0].prepare(rate, 2400); hf[1].prepare(rate, 2400);
        dcR = std::exp(-2.0 * detail::pi * 5.0 / rate);
        fastAtk = std::exp(-1.0 / (rate * 0.002));
        slowAtk = std::exp(-1.0 / (rate * 0.060));
        limiterRelease = std::exp(-1.0 / (rate * 0.080));
        reset();
    }

    void reset() noexcept {
        for (auto& c : programDelay) std::fill(c.begin(), c.end(), 0.0);
        for (auto& c : dryDelay) std::fill(c.begin(), c.end(), 0.0);
        for (auto& f : lowShelf) f.reset();
        for (auto& f : highShelf) f.reset();
        for (auto& f : hf) f.reset();
        glueComp.reset(); dynamicsComp.reset();
        dcInput.fill(0.0); dcOutput.fill(0.0);
        fastEnv = slowEnv = 0.0; limiterGain = 1.0; writeIndex = 0;
        eqCounter = 0; lastEqLows = lastEqHighs = 1.0e9;
        gainReduction = 1.0; outputPeak = 0.0;
    }

    void setParameters(const MasterParameters& p) noexcept {
        input.setTarget(detail::dbToLinear(detail::clamp(p.inputDb, -18, 18)));
        drive.setTarget(detail::clamp(p.drive, 0, 1));
        glueAmt.setTarget(detail::clamp(p.glue, 0, 1));
        dynamicsAmt.setTarget(detail::clamp(p.dynamics, 0, 1));
        lows.setTarget(detail::clamp(p.lowsDb, -6, 6));
        highs.setTarget(detail::clamp(p.highsDb, -6, 6));
        sauceAmt.setTarget(detail::clamp(p.sauce, 0, 1));
        transientsAmt.setTarget(detail::clamp(p.transients, 0, 1));
        output.setTarget(detail::dbToLinear(detail::clamp(p.outputDb, -18, 12)));
        ceiling.setTarget(detail::dbToLinear(detail::clamp(p.ceilingDb, -3, -0.1)));
        bypassMix.setTarget(p.bypass ? 1.0 : 0.0);
    }

    void beginBlock() noexcept { gainReduction = 1.0; outputPeak = 0.0; }

    StereoFrame processFrame(float inLeft, float inRight) noexcept {
        using namespace detail;
        const double rawL = static_cast<double>(inLeft);
        const double rawR = channels == 2 ? static_cast<double>(inRight) : rawL;
        double l = rawL, r = rawR;
        const double inputGain = input.next();
        const double d = drive.next();
        const double g = glueAmt.next(), dyn = dynamicsAmt.next();
        const double eqLo = lows.next(), eqHi = highs.next();
        const double s = sauceAmt.next(), tr = transientsAmt.next();
        const double outGain = output.next();
        const double absoluteCeiling = ceiling.next();
        const double targetCeiling = absoluteCeiling * 0.977237220956; // 0.2dB reconstruction margin
        const double by = bypassMix.next();
        l *= inputGain; r *= inputGain;

        // Symmetric soft saturation, level matched at its operating point.
        if (d > 1.0e-5) {
            const double amount = 1.0 + 4.0 * d;
            const double compensation = amount / std::tanh(amount);
            l += d * (std::tanh(l * amount) * compensation / amount - l);
            r += d * (std::tanh(r * amount) * compensation / amount - r);
            // Post-saturation high-pass removes potential near-DC energy.
            const double yl = l - dcInput[0] + dcR * dcOutput[0];
            const double yr = r - dcInput[1] + dcR * dcOutput[1];
            dcInput[0] = l; dcInput[1] = r;
            dcOutput[0] = yl; dcOutput[1] = yr;
            l = yl; r = yr;
        } else {
            // Keep HP memory in step when drive is later enabled.
            dcInput[0] = l; dcInput[1] = r;
            dcOutput[0] = l; dcOutput[1] = r;
        }

        // 2nd-order RBJ shelves; retune every ~0.33ms at 192 kHz, with smoothed gains.
        if (--eqCounter <= 0) {
            eqCounter = 64;
            if (std::abs(eqLo - lastEqLows) > 0.00003 || std::abs(eqHi - lastEqHighs) > 0.00003) {
                for (int c = 0; c < channels; ++c) {
                    lowShelf[static_cast<std::size_t>(c)].setShelf(rate, 115, eqLo, false);
                    highShelf[static_cast<std::size_t>(c)].setShelf(rate, 8200, eqHi, true);
                }
                lastEqLows = eqLo; lastEqHighs = eqHi;
            }
        }
        l = highShelf[0].process(lowShelf[0].process(l));
        if (channels == 2) r = highShelf[1].process(lowShelf[1].process(r));
        else r = l;

        const double detector = std::max(std::abs(l), std::abs(r));
        const double glueGain = glueComp.next(detector, g, -19, 2.5);
        const double dynamicsGain = dynamicsComp.next(detector, dyn, -26, 3.6);
        l *= glueGain * dynamicsGain;
        r *= glueGain * dynamicsGain;

        // Stereo-linked transient attack lift, no gain pumping on sustained tails.
        const double postCompressionPeak = std::max(std::abs(l), std::abs(r));
        fastEnv = fastAtk * fastEnv + (1 - fastAtk) * postCompressionPeak;
        slowEnv = slowAtk * slowEnv + (1 - slowAtk) * postCompressionPeak;
        const double onset = clamp((fastEnv - slowEnv) / (slowEnv + 0.08), 0.0, 1.3);
        const double transientGain = 1.0 + tr * 0.42 * onset;
        l *= transientGain; r *= transientGain;

        // Light band-limited harmonic sheen and mild side widening, always unity at 0.
        const double hL = l - hf[0].low(l);
        const double hR = r - hf[1].low(r);
        if (s > 1.0e-5) {
            l += 0.24 * s * (std::tanh(2.3 * hL) / 2.3 - hL);
            r += 0.24 * s * (std::tanh(2.3 * hR) / 2.3 - hR);
            if (channels == 2) {
                const double mid = 0.5 * (l + r);
                const double side = 0.5 * (l - r) * (1.0 + 0.18 * s);
                l = mid + side; r = mid - side;
            }
        }
        l *= outGain; r *= outGain;

        // Lookahead feed-forward brickwall-style limiter. Detector looks ahead by
        // delaying the programme signal; release is stereo-linked and smooth.
        const double peak = std::max(std::abs(l), std::abs(r));
        const double wanted = peak > targetCeiling ? targetCeiling / peak : 1.0;
        limiterGain = wanted < limiterGain ? wanted
            : limiterRelease * limiterGain + (1.0 - limiterRelease) * wanted;
        const auto pos = static_cast<std::size_t>(writeIndex);
        const double delayedL = programDelay[0][pos];
        const double delayedR = programDelay[1][pos];
        const double dryL = dryDelay[0][pos];
        const double dryR = dryDelay[1][pos];
        programDelay[0][pos] = l; programDelay[1][pos] = r;
        dryDelay[0][pos] = rawL; dryDelay[1][pos] = rawR;
        if (++writeIndex == lookahead) writeIndex = 0;

        double wetL = delayedL * limiterGain;
        double wetR = delayedR * limiterGain;
        // Last-resort sample clip to the user ceiling (not a certified BS.1770 TP reading).
        wetL = clamp(wetL, -absoluteCeiling, absoluteCeiling);
        wetR = clamp(wetR, -absoluteCeiling, absoluteCeiling);
        const double resultL = wetL * (1 - by) + dryL * by;
        const double resultR = wetR * (1 - by) + dryR * by;
        const double effectiveCompression = 1.0 - (1.0 - glueGain * dynamicsGain * limiterGain) * (1.0 - by);
        gainReduction = std::min(gainReduction, std::max(1.0e-9, effectiveCompression));
        outputPeak = std::max(outputPeak, std::max(std::abs(resultL), std::abs(resultR)));
        return { static_cast<float>(resultL), static_cast<float>(resultR) };
    }

    double getBlockGainReductionDb() const noexcept { return detail::clamp(-detail::linearToDb(gainReduction), 0, 48); }
    double getBlockOutputPeak() const noexcept { return outputPeak; }
    int getLookaheadSamples() const noexcept { return lookahead; }
private:
    double rate = 192000, dcR = 0.999;
    int channels = 2, lookahead = 288, writeIndex = 0, eqCounter = 0;
    std::array<std::vector<double>, 2> programDelay, dryDelay;
    detail::Smoother input, drive, glueAmt, dynamicsAmt, lows, highs, sauceAmt, transientsAmt, output, ceiling, bypassMix;
    std::array<detail::Biquad, 2> lowShelf, highShelf;
    std::array<detail::OnePole, 2> hf;
    detail::Compressor glueComp, dynamicsComp;
    std::array<double, 2> dcInput{}, dcOutput{};
    double fastEnv = 0, slowEnv = 0, fastAtk = 0, slowAtk = 0;
    double limiterGain = 1, limiterRelease = 0;
    double lastEqLows = 1.0e9, lastEqHighs = 1.0e9;
    double gainReduction = 1, outputPeak = 0;
};
} // namespace skelly
