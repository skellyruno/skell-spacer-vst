// Self-contained 15-second musical DSP regression and lossless A/B stems.
// Generated material is synthetic, not a real singer, producer track or FL Studio host render.
#include "ZeroLatencyEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
constexpr int rate=44100,nframes=15*rate;
constexpr double pi=skell::kPi;
static void u16(std::ofstream& f, uint16_t u) {f.put(static_cast<char>(u));f.put(static_cast<char>(u>>8));}
static void u32(std::ofstream& f, uint32_t u) {for(int i=0;i<4;++i)f.put(static_cast<char>(u>>(8*i)));}
static bool wav(const std::string& path, const std::vector<float>& v) {
  std::ofstream f(path,std::ios::binary);if(!f)return false;
  const uint32_t count=static_cast<uint32_t>(v.size()),bytes=count*3;
  f.write("RIFF",4);u32(f,36+bytes);f.write("WAVEfmt ",8);
  u32(f,16);u16(f,1);u16(f,2);u32(f,rate);u32(f,rate*6);u16(f,6);u16(f,24);
  f.write("data",4);u32(f,bytes);
  for (const float s : v) {
    const double clamped=std::max(-1.,std::min(1.,static_cast<double>(s)));
    const int32_t p=static_cast<int32_t>(std::lrint(clamped*8388607.));
    f.put(static_cast<char>(p));f.put(static_cast<char>(p>>8));f.put(static_cast<char>(p>>16));
  }
  return f.good();
}
int main(int argc,char** argv) {
  const std::string dir=argc>1?argv[1]:".";
  skell::ZeroLatencyEngine e;e.prepare(rate);
  skell::SpacerParams p;p.amount=.72f;p.smooth=.22f;p.attackMs=6.f;p.releaseMs=145.f;
  // Keep existing synthetic reference unchanged; opt-in alternate captures use identical dry/key.
  const std::string mode=argc>2?argv[2]:"legacy";
  if (mode=="sensitive") p.levelSensitivity=1.f;
  else if (mode=="fast") {p.levelSensitivity=1.f;p.attackMs=1.f;}
  else if (mode=="fast-legacy") p.attackMs=1.f;
  else if(mode!="legacy") return 2;
  e.setParams(p);
  std::vector<float> dry(nframes*2),wet(nframes*2),key(nframes*2);
  double diff=0,raw=0,rendered=0;float peak=0;bool finite=true;
  constexpr int block=256;
  for(int start=0;start<nframes;start+=block) {
    const int count=std::min(block,nframes-start);
    float a[block],b[block],scA[block],scB[block];
    for(int i=0;i<count;++i) {
      const int index=start+i;const double t=static_cast<double>(index)/rate;
      // Slowly moving, stereo sustained three-note pad and controlled kick/bass.
      const double pad=.07*std::sin(2*pi*196*t)+.055*std::sin(2*pi*246.94*t)
                      +.06*std::sin(2*pi*293.66*t)+.018*std::sin(2*pi*392*t);
      const double side=.014*std::sin(2*pi*370*t+.15*std::sin(2*pi*.15*t));
      const double phase=std::fmod(t,.5);
      const double kick=.13*std::exp(-24*phase)*std::sin(2*pi*(52*phase+14*phase*phase));
      const double input=pad+kick;
      a[i]=static_cast<float>(input+side);
      b[i]=static_cast<float>(input-side);
      // Pulse-like formant/harmonic melodic sidechain; not a voice recording.
      const double syllable=std::fmod(t,.78);
      const double env=syllable<.37 ? std::min(1.,syllable/.018)*std::exp(-3.*syllable):0.;
      const double form=.4*std::sin(2*pi*175*t)+.24*std::sin(2*pi*525*t)
                        +.13*std::sin(2*pi*1050*t)+.07*std::sin(2*pi*2100*t);
      scA[i]=scB[i]=static_cast<float>(env*form);
      dry[2*index]=a[i];dry[2*index+1]=b[i];
      key[2*index]=scA[i];key[2*index+1]=scB[i];
    }
    float* track[2]={a,b};const float* sidechain[2]={scA,scB};
    e.process(track,sidechain,2,2,count);
    for(int i=0;i<count;++i) {
      const int idx=start+i;
      wet[2*idx]=a[i];wet[2*idx+1]=b[i];
      for(int c=0;c<2;++c) {
        const float val=c?b[i]:a[i];
        const double orig=dry[2*idx+c];
        diff+=(val-orig)*(val-orig);raw+=orig*orig;rendered+=val*val;
        peak=std::max(peak,std::abs(val));
        if(!std::isfinite(val))finite=false;
      }
    }
  }
  const bool valid=finite&&peak<1.f&&diff/raw>0.0001&&rendered/raw<1.1&&e.latencySamples()==0;
  const std::string outputName=mode=="legacy"?"SkellSpacer_DUCKED_15s_24bit.wav":"SkellSpacer_"+mode+"_15s_24bit.wav";
  const bool saved=wav(dir+"/"+outputName,wet) && (mode!="legacy" ||
                   (wav(dir+"/SkellSpacer_DRY_15s_24bit.wav",dry)&&wav(dir+"/SkellSpacer_KEY_15s_24bit.wav",key)));
  std::printf("[%s] 15s stereo synthetic A/B: dry RMS %.5f, wet RMS %.5f, rms difference %.5f, wet peak %.5f, latency %d samples, save=%s\n",
              valid&&saved?"PASS":"FAIL",std::sqrt(raw/(2.*nframes)),std::sqrt(rendered/(2.*nframes)),
              std::sqrt(diff/(2.*nframes)),peak,e.latencySamples(),saved?"yes":"no");
  return valid&&saved?0:1;
}
