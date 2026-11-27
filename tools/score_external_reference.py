#!/usr/bin/env python3
"""Validate an owner-rendered original reference against an exact research case.
The render must contain the processed bed only, not the summed vocal.
This cannot authenticate a vendor binary or prove a listening preference.
"""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
import soundfile as sf
from mix_research import read, aligned, energy, conflict, conflict_weights, db_ratio


def score(case, wet_path, metadata, bypass_path=None):
    report=json.loads((case/'report.json').read_text())
    expected=report['source_sha256']
    if metadata.get('source_sha256')!=expected or metadata.get('offset_samples')!=0:
        raise ValueError('source identity or offset mismatch')
    if metadata.get('settings')!={'amount_percent':35,'attack_ms':10,'release_ms':100,'mode':'LR','pan':0,'full_band':True,'freeze':False}:
        raise ValueError('reference settings mismatch')
    if not metadata.get('plugin_version') or not metadata.get('plugin_name'):
        raise ValueError('reference identity required')
    for name,sha in expected.items():
        if hashlib.sha256((case/name).read_bytes()).hexdigest()!=sha:
            raise ValueError('source hash mismatch')
    bed,rate=read(case/'source_bed.wav');key=aligned(case/'source_key.wav',bed.shape,rate)
    wet=aligned(wet_path,bed.shape,rate);vocal=aligned(case/'listen_key.wav',bed.shape,rate)
    if rate!=report['rate'] or len(bed)!=report['frames'] or energy(wet)<1e-12:
        raise ValueError('reference render frame/rate/energy mismatch')
    alignment={'verified':False,'reason':'no bypass render supplied; offset is operator-declared only'}
    if bypass_path is not None:
        bypass=aligned(bypass_path,bed.shape,rate)
        max_error=float(np.max(np.abs(bypass-bed)))
        # Allow ordinary PCM quantization only, not normalization or shifting.
        if max_error>2e-5:
            raise ValueError(f'bypass does not match source at offset zero: max sample error {max_error:.8g}')
        alignment={'verified':True,'bypass_max_sample_error':max_error,
                   'scope':'bypass route matches source; wet-path compensation still operator-declared'}
    w=conflict_weights(key);before=conflict(bed,w)
    if before<1e-12: raise ValueError('no measurable source overlap')
    result={'plugin':metadata['plugin_name'],'version':metadata['plugin_version'],
            'conflict_reduction':1-conflict(wet,w)/before,'energy_retention':energy(wet)/energy(bed),
            'peak_dbfs':db_ratio(float(np.max(np.abs(wet)))**2,1.),'frames':len(wet),'rate':rate,'offset_samples':0,
            'alignment':alignment}
    # RMS-match reference and Smart from their unnormalized bed signals,
    # adding exactly the same direct listening key; one common headroom gain.
    smart=aligned(case/'smart_bed.wav',bed.shape,rate)+vocal
    reference=wet+vocal;gain=np.sqrt(energy(smart)/max(energy(reference),1e-12))
    headroom=min(1.,.95/max(float(np.max(np.abs(smart))),float(np.max(np.abs(reference)))*gain))
    sf.write(case/'reference_matched_mix.wav',reference*gain*headroom,rate,subtype='FLOAT')
    sf.write(case/'smart_reference_matched_mix.wav',smart*headroom,rate,subtype='FLOAT')
    result['rms_match_gain']=float(gain);result['common_headroom']=float(headroom)
    (case/'external_reference_score.json').write_text(json.dumps(result,indent=2))
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--case',type=Path,required=True);p.add_argument('--wet',type=Path,required=True);p.add_argument('--metadata',type=Path,required=True)
    p.add_argument('--bypass',type=Path,help='same-route bypass bed, unnormalized at offset zero')
    a=p.parse_args();print(json.dumps(score(a.case,a.wet,json.loads(a.metadata.read_text()),a.bypass),indent=2))
