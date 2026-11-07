#ifdef NDEBUG
#undef NDEBUG
#endif
#include <SkellCore/Core.h>
#include <cassert>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <atomic>

// Detect accidental heap allocation inside realtime processFrame().
std::atomic<std::size_t> allocations{0};
void* operator new(std::size_t n) {
    ++allocations;
    if (void* p = std::malloc(n)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    ++allocations;
    if (void* p = std::malloc(n)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main() {
    constexpr double pi=3.14159265358979323846;
    skellcore::FDNReverb reverb;
    auto before = reverb.processFrame(0.4f,-0.25f);
    assert(before[0]==.4f && before[1]==-.25f);
    skellcore::TruePeakLimiter unprepared;
    float input[2]{.3f,-.5f}, output[2]{};
    unprepared.processFrame(input,output,2);
    assert(output[0]==input[0] && output[1]==input[1]);
    assert(unprepared.latencySamples()==0);
    unprepared.processFrame(nullptr,output,2);
    unprepared.processFrame(input,nullptr,2);
    // Invalid sample rates must NOT request enormous allocations or create NaN filters.
    reverb.prepare(std::numeric_limits<double>::infinity());
    reverb.setParameters(NAN,NAN,NAN);
    auto safe=reverb.processFrame(NAN,.25f);
    assert(std::isfinite(safe[0]) && std::isfinite(safe[1]));
    for(int sr : {44100,48000,96000,192000}) {
        for(auto quality : {skellcore::TruePeakLimiter::Quality::Economy,
                            skellcore::TruePeakLimiter::Quality::Standard}) {
            skellcore::TruePeakLimiter limiter;
            limiter.prepare(sr,2,quality);
            limiter.setCeilingDb(-1.f);
            assert(limiter.latencySamples()>0 && limiter.latencySamples()<sr/30);
            assert(limiter.estimatedWorkingBytes()<32768);
            const auto countBefore=allocations.load();
            float maxSample=0.f;
            for(int i=0;i<100000;++i) {
                input[0] = 1.6f * float(std::sin(2*pi*997*i/sr));
                input[1] = (i % 15000 == 0) ? 9.f : -input[0];
                limiter.processFrame(input, output, 2);
                assert(std::isfinite(output[0]) && std::isfinite(output[1]));
                maxSample=std::max(maxSample,std::abs(output[0]));
            }
            assert(allocations.load()==countBefore);
            assert(maxSample<.895f); // sampled peak only, not inter-sample compliance
        }
    }
    reverb.prepare(48000);
    reverb.setParameters(1.6f,.3f,.4f);
    auto allocationBefore=allocations.load();
    for(int i=0;i<100000;++i) {
        const auto x=reverb.processFrame(i==0?1.f:0.f,0.f);
        assert(std::isfinite(x[0])&&std::isfinite(x[1]));
    }
    assert(allocations.load()==allocationBefore);
    std::cout<<"Low-CPU realtime safety checks passed\n";
}
