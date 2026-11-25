#include "DetectorResponse.h"
#include <cassert>
#include <cstdio>
#include <limits>
using skell::DetectorResponse;
int main() {
  for(float x : {-64.f,-1.f,-0.1f,0.f,0.1f,1.f,64.f}) assert(DetectorResponse::safeKey(x)==x);
  for(float x : {-1.e30f,1.e30f,65.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) assert(DetectorResponse::safeKey(x)==0.f);
  assert(DetectorResponse::fastBlend(0.f,1.f)==0.f);
  assert(DetectorResponse::fastBlend(1.f,4.f)==0.f);
  assert(DetectorResponse::fastBlend(1.f,1.f)==1.f);
  assert(DetectorResponse::fastBlend(.5f,1.f)==.5f);
  for(double fs : {44100.,48000.,96000.,192000.}) for(float hz : {20.f,100.f,1000.f,9000.f}) {
    const double tau=std::max(.004,2.5/static_cast<double>(hz));
    const float classic=static_cast<float>(-std::expm1(-1./(fs*tau)));
    const float fast=DetectorResponse::fastCoefficient(fs,hz);
    assert(fast>classic&&fast<1.f);
    assert(DetectorResponse::coefficient(classic,fast,0.f)==classic);
    assert(DetectorResponse::coefficient(classic,fast,1.f)==fast);
    auto onset=[&](float c){float p=0;int i=0;while(p<.63212055f&&i<static_cast<int>(fs)){p+=(1.f-p)*c;++i;}return 1000.*i/fs;};
    const double oldMs=onset(classic),newMs=onset(fast);
    assert(newMs<oldMs);
    std::printf("%.0f Hz / %.0f Hz band: power step 63.2%% %.4f -> %.4f ms\n",fs,hz,oldMs,newMs);
  }
}
