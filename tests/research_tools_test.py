import sys
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from mix_research import steer_mono_safe, energy, db_ratio, conflict_weights, conflict
fs=48000
t=np.arange(fs)/fs
x=(.15*np.sin(2*np.pi*1000*t)).astype(np.float32)
main=np.column_stack((x,x));main[0]=[.8,-.8]
key=np.column_stack((x,np.zeros_like(x)))
wet,strength=steer_mono_safe(main,key,fs)
assert strength>0 and np.max(np.abs(wet.mean(axis=1)-main.mean(axis=1)))<2e-7
assert abs(db_ratio(energy(wet),energy(main)))<=.25
assert np.max(np.abs(wet))<=np.max(np.abs(main))+1e-6
w=conflict_weights(key)
reduction=1-conflict(wet,w)/conflict(main,w)
assert reduction>.1
print(f'PASS: offline mono-preserving steering, {reduction:.4%} modeled conflict reduction, {db_ratio(energy(wet),energy(main)):.4f} dB energy change')
