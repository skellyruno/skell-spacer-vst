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

# Held-out validation: first region favours a candidate; second region
# deliberately has no reduction. Reject it without selecting on the test set.
from mix_research import select_held_out
from score_external_reference import score
import tempfile, hashlib, json, soundfile as sf
rng=np.random.default_rng(7)
a=(rng.normal(0,.06,(48000,2))).astype(np.float32)
k=a.copy(); smart=a*.8; contender=a*.81; contender[int(len(a)*.6):]=a[int(len(a)*.6):]
candidates={'smart':None,'contender':None}
choice,validation=select_held_out(a,k,{'smart':smart,'contender':contender},candidates)
assert choice=='smart' and validation['training_choice']=='contender' and validation['status']=='fallback_smart'
choice,validation=select_held_out(a,k,{'smart':smart,'contender':a*.81},candidates)
assert choice=='contender' and validation['status']=='passed'
print('PASS: training selection and untouched holdout fallback')
with tempfile.TemporaryDirectory() as td:
    case=Path(td)
    for name,arr in [('source_bed.wav',a),('source_key.wav',k),('listen_key.wav',k),('smart_bed.wav',smart),('wet.wav',smart),('bypass.wav',a),('shifted.wav',np.roll(a,1,axis=0))]:
        sf.write(case/name,arr,fs,subtype='FLOAT')
    hashes={n:hashlib.sha256((case/n).read_bytes()).hexdigest() for n in ('source_bed.wav','source_key.wav','listen_key.wav')}
    (case/'report.json').write_text(json.dumps({'source_sha256':hashes,'rate':fs,'frames':len(a)}))
    meta={'source_sha256':hashes,'offset_samples':0,'plugin_name':'synthetic fixture','plugin_version':'1',
          'settings':{'amount_percent':35,'attack_ms':10,'release_ms':100,'mode':'LR','pan':0,'full_band':True,'freeze':False}}
    assert score(case,case/'wet.wav',meta)['alignment']['verified'] is False
    assert score(case,case/'wet.wav',meta,case/'bypass.wav')['alignment']['verified'] is True
    try:
        score(case,case/'wet.wav',meta,case/'shifted.wav')
    except ValueError:
        pass
    else:
        raise AssertionError('shifted bypass accepted')
print('PASS: reference bypass identity and offset rejection')

# Verify K-weighted matching (not merely RMS), common headroom and rejection.
from listening_loudness import match
low=.3*np.sin(2*np.pi*80*t); high=.3*np.sin(2*np.pi*3000*t)
lo=np.column_stack((low,low)).astype(np.float32)
hi=np.column_stack((high,high)).astype(np.float32)
matched,report=match({'smart':lo,'high':hi},fs)
assert abs(report['before_lufs']['smart']-report['before_lufs']['high'])>2.
assert report['spread_lu']<=.10
assert max(report['estimated_4x_peak_db'].values())<=-.999
try:
    match({'smart':lo,'silent':np.zeros_like(lo)},fs)
except ValueError:
    pass
else:
    raise AssertionError('silent candidate accepted')
print('PASS: gated LUFS matching, oversampled peak ceiling, silence rejection')
