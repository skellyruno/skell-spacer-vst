// Offline research renderer. Input/output: stereo interleaved little-endian
// float32 at declared rate, equal frames, exact zero sample offset.
// No audio or file I/O on the plugin's realtime thread.
#include "ZeroLatencyEngine.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <cstdlib>
static std::vector<float> read(const char* path) {
  std::ifstream f(path,std::ios::binary|std::ios::ate);
  if(!f)throw std::runtime_error("input open failed");
  const auto bytes=f.tellg();
  if(bytes<=0||bytes%8!=0||bytes>static_cast<std::streamoff>(48000000))throw std::runtime_error("invalid or oversized stereo float input");
  std::vector<float> v(static_cast<size_t>(bytes)/sizeof(float));
  f.seekg(0);f.read(reinterpret_cast<char*>(v.data()),bytes);
  if(!f)throw std::runtime_error("input read failed");
  for(float x:v)if(!std::isfinite(x)||std::abs(x)>4.f)throw std::runtime_error("invalid audio sample");
  return v;
}
int main(int argc,char** argv){
 try {
  if(argc!=10&&argc!=13)throw std::runtime_error("usage: render_research main.f32 key.f32 out.f32 rate amount smart cleanKey phaseProtect stereoDetail [humanEar componentProtect refinedModel]");
  auto m=read(argv[1]),k=read(argv[2]);
  if(m.size()!=k.size())throw std::runtime_error("unequal source lengths");
  const double fs=std::stod(argv[4]);if(fs<44100.||fs>192000.)throw std::runtime_error("unsupported research rate");
  skell::SpacerParams p; p.amount=std::stof(argv[5]);p.smart=std::stof(argv[6]);p.cleanKey=std::stof(argv[7]);p.phaseProtect=std::stof(argv[8]);p.stereoDetail=std::stof(argv[9]);
  if(argc==13){p.humanEar=std::stof(argv[10]);p.componentProtect=std::stof(argv[11]);const std::string model=argv[12];if(model!="0"&&model!="1")throw std::runtime_error("invalid model");p.refinedPerception=model=="1";}
  for(float v:{p.amount,p.smart,p.cleanKey,p.phaseProtect,p.stereoDetail,p.humanEar,p.componentProtect})if(!std::isfinite(v)||v<0.f||v>1.f)throw std::runtime_error("invalid setting");
  p.attackMs=10.f;p.releaseMs=100.f;p.smooth=.3f;
  skell::ZeroLatencyEngine e;e.prepare(fs);e.setParams(p);
  constexpr int block=128;
  for(size_t n=0;n<m.size()/2;n+=block){
    const int frames=static_cast<int>(std::min<size_t>(block,m.size()/2-n));
    float l[block],r[block],kl[block],kr[block];
    for(int i=0;i<frames;++i){const size_t q=2*(n+static_cast<size_t>(i));l[i]=m[q];r[i]=m[q+1];kl[i]=k[q];kr[i]=k[q+1];}
    float* main[]={l,r};const float* key[]={kl,kr};e.process(main,key,2,2,frames);
    for(int i=0;i<frames;++i){const size_t q=2*(n+static_cast<size_t>(i));if(!std::isfinite(l[i])||!std::isfinite(r[i]))throw std::runtime_error("nonfinite output");m[q]=l[i];m[q+1]=r[i];}
  }
  std::ofstream f(argv[3],std::ios::binary);f.write(reinterpret_cast<const char*>(m.data()),static_cast<std::streamsize>(m.size()*sizeof(float)));
  if(!f||e.latencySamples()!=0)throw std::runtime_error("output write or latency failure");
  std::cout<<"PASS: "<<m.size()/2<<" frames, zero reported latency\n";
 }catch(const std::exception& x){std::cerr<<x.what()<<'\n';return 1;}
}
