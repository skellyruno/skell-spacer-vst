"""Offline stereo integrated-loudness matching; no realtime dependency.
Requires pyloudnorm, numpy and scipy. Four-times polyphase peak is an
estimate, not a certified true-peak meter. No compressor or limiter.
"""
import numpy as np
import pyloudnorm as pyln
from scipy.signal import resample_poly


def match(mixes, rate, reference='smart', tolerance=.10, peak_db=-1.):
    if reference not in mixes or len(mixes)<2 or not 44100<=rate<=192000:
        raise ValueError('invalid listening set')
    meter=pyln.Meter(rate, filter_class='DeMan')
    shape=mixes[reference].shape
    for x in mixes.values():
        if x.shape!=shape or x.ndim!=2 or x.shape[1]!=2 or len(x)<rate*.4 or not np.isfinite(x).all():
            raise ValueError('invalid aligned stereo listening material')
    def loud(x):
        l=float(meter.integrated_loudness(x.astype(np.float64)))
        if not np.isfinite(l): raise ValueError('silent or ungateable listening material')
        return l
    def peak(x):
        # Include actual samples and oversampled interpolation.
        return max(float(np.max(np.abs(x))),float(np.max(np.abs(resample_poly(x,4,1,axis=0)))))
    before={n:loud(x) for n,x in mixes.items()}
    target=before[reference]
    gains={n:10**((target-v)/20) for n,v in before.items()}
    if any(abs(target-v)>12. for v in before.values()):
        raise ValueError('candidate loudness differs by more than 12 LU')
    peaks={n:peak(x) for n,x in mixes.items()}
    limit=10**(peak_db/20)
    # Common attenuation for headroom; check the absolute gate after scaling.
    for _ in range(4):
        headroom=min(1.,limit/max(peaks[n]*gains[n] for n in mixes))
        out={n:(x*gains[n]*headroom).astype(np.float32) for n,x in mixes.items()}
        after={n:loud(x) for n,x in out.items()}
        if max(after.values())-min(after.values())<=tolerance:
            return out,{'method':'BS.1770 gated integrated LUFS (DeMan K weighting)',
                'reference':reference,'before_lufs':before,'after_lufs':after,
                'spread_lu':max(after.values())-min(after.values()),
                'gain':{n:float(gains[n]*headroom) for n in mixes},
                'common_headroom':float(headroom),
                'estimated_4x_peak_db':{n:float(20*np.log10(max(peak(x),1e-20))) for n,x in out.items()},
                'peak_limit_db':peak_db}
        for n in gains:
            gains[n]*=10**((after[reference]-after[n])/20)
    raise ValueError('integrated loudness matching did not converge')
