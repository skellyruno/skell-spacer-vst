#!/usr/bin/env python3
"""Private, offline Spacer A/B and settings search. No model or listening claim.
Requires numpy, scipy, soundfile and the compiled tools/render_research.cpp.
Inputs must already be explicitly time-aligned, stereo and same sample rate.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import numpy as np
import soundfile as sf
from scipy.signal import stft, istft


def read(path):
    data, rate = sf.read(path, dtype='float32', always_2d=True)
    if data.shape[1] != 2 or not np.isfinite(data).all() or not len(data):
        raise ValueError('expected finite nonempty stereo audio')
    return data, rate


def aligned(path, shape, rate):
    data, fs = read(path)
    if data.shape != shape or fs != rate:
        raise ValueError(f'alignment contract failed: {path.name}')
    return data


def energy(x):
    return float(np.mean(np.square(x, dtype=np.float64)))


def db_ratio(a, b):
    return float(10*np.log10(max(a, 1e-20)/max(b, 1e-20)))


def spectrum(x):
    return stft(x.T, nperseg=1024, noverlap=768, axis=-1)[2]


def conflict_weights(key):
    p = np.abs(spectrum(key))**2
    peak = p.max(axis=1, keepdims=True)
    active = p.sum(axis=1, keepdims=True) > max(float(p.sum(axis=1).max())*.005, 1e-12)
    # Relative spectral power, per stereo channel and frame. Not a human ear score.
    return p / np.maximum(peak, 1e-12) * active


def conflict(audio, weights):
    return float(np.sum(np.abs(spectrum(audio))**2 * weights, dtype=np.float64))


def steer_mono_safe(main, key, rate):
    # Offline frequency-dependent side tilt. The mid is never modified.
    # Retain the input peak and total stereo power within +/-0.25 dB by
    # backing off the steering strength; not a zero-latency plugin algorithm.
    mid = .5*(main[:, 0]+main[:, 1]); side = .5*(main[:, 0]-main[:, 1])
    _, _, m = stft(mid, fs=rate, nperseg=1024, noverlap=768)
    k = np.mean(np.abs(spectrum(key))**2, axis=2)
    tilt = .15*(k[1]-k[0])/np.maximum(k.sum(axis=0), 1e-12)
    _, d = istft(m*tilt[:, None], fs=rate, nperseg=1024, noverlap=768)
    d = d[:len(mid)]
    original = energy(main); peak = float(np.max(np.abs(main)))
    strength = 1.
    for _ in range(24):
        s = side+strength*d
        out = np.column_stack((mid+s, mid-s)).astype(np.float32)
        if abs(db_ratio(energy(out), original)) <= .25 and np.max(np.abs(out)) <= peak+1e-6:
            return out, strength
        strength *= .5
    return main.copy(), 0.


def render(exe, main, key, rate, settings, scratch):
    a,b,c = [scratch/name for name in ('main.f32','key.f32','out.f32')]
    main.astype('<f4').tofile(a); key.astype('<f4').tofile(b)
    subprocess.run([str(exe),str(a),str(b),str(c),str(rate),*map(str,settings)],check=True,stdout=subprocess.DEVNULL)
    out = np.fromfile(c,dtype='<f4')
    if out.size != main.size or not np.isfinite(out).all():
        raise ValueError('invalid renderer output')
    return out.reshape(main.shape)


def research(exe, main, key, rate, out, oracle=None, separator=None):
    if len(main) < 1024 or energy(main)<1e-12 or energy(key)<1e-12:
        raise ValueError('silent or too short research input')
    out.mkdir(parents=True,exist_ok=True)
    weights=conflict_weights(key); before=conflict(main,weights)
    if before<1e-12: raise ValueError('no measurable overlapping energy')
    candidates={'classic':(.35,0,0,0,0),'smart':(.35,1,0,0,0),
                'clean':(.35,1,.65,0,0),'phase':(.35,1,0,.65,0),
                'stereo':(.35,1,0,0,1),'combined':(.35,1,.65,.65,1)}
    for a in (.25,.35,.45,.55):
        for c in (0,.5,1):
            candidates[f'a{int(a*100)}_c{int(c*100)}']=(a,1,c,.65,1)
    results={}; audio={}
    with tempfile.TemporaryDirectory() as tmp:
        scratch=Path(tmp)
        for name,settings in candidates.items():
            audio[name]=render(exe,main,key,rate,settings,scratch)
        if oracle is not None:
            audio['direct_key_oracle']=render(exe,main,oracle,rate,candidates['smart'],scratch)
        if separator is not None:
            audio['separator_key']=render(exe,main,separator,rate,candidates['smart'],scratch)
    audio['invisible_steer'], strength=steer_mono_safe(main,key,rate)
    for name,x in audio.items():
        residual=conflict(x,weights)
        results[name]={'conflict_reduction':1-residual/before,
                       'energy_retention':energy(x)/energy(main),
                       'peak_dbfs':db_ratio(float(np.max(np.abs(x)))**2,1.),
                       'mono_difference_rms':float(np.sqrt(energy(x.mean(axis=1)-main.mean(axis=1))))}
        if name in candidates: results[name]['settings']=candidates[name]
        if oracle is not None:
            results[name]['oracle_error_rms']=float(np.sqrt(energy(x-audio['direct_key_oracle'])))
    target=max(0.,results['smart']['conflict_reduction']*.90)
    eligible=[n for n in candidates if n!='classic' and results[n]['conflict_reduction']>=target]
    choice=max(eligible,key=lambda n:results[n]['energy_retention']) if eligible else None
    # Private listening files: add the same key at the same alignment. Use
    # matched RMS and one common headroom scalar, not independent clipping.
    mixkey=oracle if oracle is not None else key
    chosen=list(dict.fromkeys(['classic','smart','clean','phase','stereo','combined',choice or 'smart','invisible_steer']+(['direct_key_oracle'] if oracle is not None else [])+(['separator_key'] if separator is not None else [])))
    mix={n:audio[n]+mixkey for n in chosen}
    target_rms=np.sqrt(energy(mix['smart']))
    gains={n:target_rms/max(np.sqrt(energy(x)),1e-12) for n,x in mix.items()}
    headroom=min(1.,.95/max(float(np.max(np.abs(x)))*gains[n] for n,x in mix.items()))
    for n,x in mix.items():
        sf.write(out/f'{n}_mix.wav',x*gains[n]*headroom,rate,subtype='FLOAT')
        sf.write(out/f'{n}_bed.wav',audio[n],rate,subtype='FLOAT')
    sf.write(out/'source_bed.wav',main,rate,subtype='FLOAT');sf.write(out/'source_key.wav',key,rate,subtype='FLOAT')
    sf.write(out/'listen_key.wav',mixkey,rate,subtype='FLOAT')
    report={'rate':rate,'frames':len(main),'offset_samples':0,'metric':'relative spectral overlap proxy; not perceptual clarity',
            'settings_order':['amount','smart','cleanKey','phaseProtect','stereoDetail'],
            'assistant_recommendation':choice,'minimum_conflict_reduction':target,'steering_strength':strength,
            'listening_rms_gains':{n:float(g*headroom) for n,g in gains.items()},'results':results}
    report['source_sha256']={n:hashlib.sha256((out/n).read_bytes()).hexdigest() for n in ('source_bed.wav','source_key.wav','listen_key.wav')}
    (out/'report.json').write_text(json.dumps(report,indent=2))
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('renderer','bed','key','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--oracle',type=Path);p.add_argument('--separator',type=Path)
    a=p.parse_args();x,rate=read(a.bed);key=aligned(a.key,x.shape,rate)
    oracle=aligned(a.oracle,x.shape,rate) if a.oracle else None
    sep=aligned(a.separator,x.shape,rate) if a.separator else None
    r=research(a.renderer.resolve(),x,key,rate,a.out,oracle,sep)
    print(json.dumps({'recommendation':r['assistant_recommendation'],'results':r['results']},indent=2))


if __name__=='__main__':main()
