#!/usr/bin/env python3
"""Compare two offline Spacer renderers at equal musical-bed energy.
Private owner-provided aligned stereo WAV only. No perceptual/stem ground truth
is implied. Model2 baseline and candidate must accept render_research CLI.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import numpy as np
import soundfile as sf
from scipy.signal import stft

FS=48000
SETTINGS=['1','.5','.5','1','.7','.7','1']

def energy(x):
    return float(np.mean(x.astype(np.float64)**2))

def read(path):
    x,rate=sf.read(path,dtype='float32',always_2d=True)
    if rate!=FS or x.shape[1]!=2 or not FS//2<=len(x)<=FS*60 or not np.isfinite(x).all() or energy(x)<1e-12 or np.max(np.abs(x))>1:
        raise ValueError('Expected finite non-silent 48 kHz stereo audio, 0.5–60 s, within +/-1')
    return x

def metrics(dry,key):
    def powers(x):
        f,_,z=stft(x.T,fs=FS,nperseg=2048,noverlap=1792,axis=-1)
        return f,np.abs(z.astype(np.complex128))**2
    f,p=powers(dry);_,q=powers(key)
    erb=21.4*np.log10(1+.00437*f)
    edges=np.arange(0,erb[-1]+1,1.)
    ids=np.clip(np.searchsorted(edges,erb,side='right')-1,0,len(edges)-2)
    centers=(edges[:-1]+edges[1:])*.5;delta=centers[:,None]-centers[None,:]
    spread=np.exp(-np.abs(delta)/np.where(delta>=0,1.4,.7));spread[np.abs(delta)>5]=0
    def group(a):return np.stack([a[:,ids==i].sum(axis=1) for i in range(len(centers))],axis=1)
    k=group(q);kw=k/np.maximum(k.sum(),1e-20);floor=max(k.max()*1e-4,1e-15)
    def mask(a):return float(np.sum(kw*np.log1p(np.einsum('ij,cjt->cit',spread,group(a))/(k+floor))))
    baseline=mask(p)
    direct_weights=q/np.maximum(q.max(axis=1,keepdims=True),1e-15)
    direct_weights*=q.sum(axis=1,keepdims=True)>max(q.sum(axis=1).max()*.005,1e-12)
    direct=float((p*direct_weights).sum())
    flux=np.maximum(np.diff(np.sqrt(p.sum(axis=0)),axis=-1,prepend=0),0)
    frame_flux=flux.sum(axis=0);active=np.flatnonzero(frame_flux>=np.quantile(frame_flux,.85));active=active[active>3]
    fw=flux[:,active];fw=fw/np.maximum(fw.sum(),1e-20)
    transient=float((p.sum(axis=0)[:,active]*fw).sum())
    if min(baseline,direct,transient)<=1e-15:raise ValueError('No measurable mask/overlap/flux')
    def measure(y):
        _,a=powers(y)
        return {'retention_pct':100*energy(y)/energy(dry),'auditory_mask_reduction_pct':100*(1-mask(a)/baseline),'direct_overlap_reduction_pct':100*(1-float((a*direct_weights).sum())/direct),'transient_power_retained_pct':100*float((a.sum(axis=0)[:,active]*fw).sum())/transient,'transient_flux_frames':len(active)}
    return measure

def benchmark(baseline,candidate,bed,key,out):
    x,k=read(bed),read(key)
    if x.shape!=k.shape:raise ValueError('Unequal input lengths')
    out.mkdir(parents=True,exist_ok=True);measure=metrics(x,k)
    with tempfile.TemporaryDirectory() as tmp:
        tmp=Path(tmp);x.astype('<f4').tofile(tmp/'bed.f32');k.astype('<f4').tofile(tmp/'key.f32')
        def render(exe,amount):
            dest=tmp/'out.f32'
            subprocess.run([str(exe.resolve()),str(tmp/'bed.f32'),str(tmp/'key.f32'),str(dest),str(FS),str(amount),*SETTINGS],check=True,stdout=subprocess.DEVNULL)
            y=np.fromfile(dest,'<f4')
            if y.size!=x.size or not np.isfinite(y).all():raise ValueError('Invalid renderer output')
            return y.reshape(x.shape)
        old=render(baseline,.35);base=measure(old);target=base['retention_pct'];lo,hi=.01,.98;best=None
        # Calibration uses only energy, never either quality metric. The target
        # is local to the case; no held-out algorithm selection or retuning.
        for it in range(12):
            amount=.35 if it==0 else (lo+hi)/2
            y=render(candidate,amount);ret=100*energy(y)/energy(x);err=abs(ret-target)
            if best is None or err<best[0]:best=(err,amount,y.copy())
            if err<.003:break
            if ret>target:lo=amount
            else:hi=amount
        err,amount,y=best
        if err>.01:raise ValueError(f'Could not match retention within 0.01 percentage points: {err}')
        new=measure(y)
    report={'source_sha256':{n:hashlib.sha256(p.read_bytes()).hexdigest() for n,p in [('bed',bed),('key',key)]},'rate':FS,'frames':len(x),'source_offset_samples':0,'baseline':dict(base,amount=.35),'candidate':dict(new,amount=amount,retention_error_pp=err),'settings':{'smart':1,'cleanKey':.5,'phaseProtect':.5,'stereoDetail':1,'humanEar':.7,'componentProtect':.7,'refinedModel':1},'gate':{'retention_matched':True,'mask_nonregression':new['auditory_mask_reduction_pct']>=base['auditory_mask_reduction_pct'],'transient_nonregression':new['transient_power_retained_pct']>=base['transient_power_retained_pct']},'limitations':'Independent ERB-weighted STFT proxy and dry-defined positive spectral flux; not a listener score or isolated stem test.'}
    sf.write(out/'baseline_bed.wav',old,FS,subtype='FLOAT');sf.write(out/'candidate_bed.wav',y,FS,subtype='FLOAT')
    (out/'report.json').write_text(json.dumps(report,indent=2));return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('baseline','candidate','bed','key','out'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();print(json.dumps(benchmark(a.baseline,a.candidate,a.bed,a.key,a.out),indent=2))
