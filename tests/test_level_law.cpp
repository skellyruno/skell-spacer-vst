#include "LevelSensitivity.h"
#include <cassert>
#include <cstdio>
#include <limits>
int main(){
 using skell::LevelSensitivity;
 for(float fs:{44100.f,48000.f,96000.f,192000.f}) {
  LevelSensitivity s; const float step=fs<=50000?16.f:fs<=100000?32.f:64.f;
  s.prepare(step/fs); assert(s.advance(0)==0);
  float last=0;for(int i=0;i<int(fs/step*.25f);++i){float v=s.advance(1);assert(v>=last&&v<=1);last=v;}
  assert(last>.999f);s.reset();assert(s.advance(0)==0);
 }
 float last=0;
 for(float db:{-60.f,-40.f,-30.f,-24.f,-20.f,-12.f,-6.f,0.f}){
  float p=std::pow(10.f,db/10.f);float v=LevelSensitivity::scale(p,1.f);
  assert(v>last&&v<=1.f); assert(LevelSensitivity::scale(p,0.f)==1.f);
  assert(std::abs(LevelSensitivity::scale(p,.5f)-(1.f+v)*.5f)<1e-6f);
  std::printf("%.0f dBFS: %.6f target fraction\n",db,v);last=v;
 }
 assert(std::abs(LevelSensitivity::scale(LevelSensitivity::kneePower,1.f)-.5f)<1e-6f);
 assert(LevelSensitivity::scale(std::numeric_limits<float>::quiet_NaN(),1.f)==0.f);
 assert(LevelSensitivity::scale(std::numeric_limits<float>::infinity(),0.f)==1.f);
}
