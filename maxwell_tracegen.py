from scipy.stats import maxwell
import numpy as np
import os
import argparse

parser = argparse.ArgumentParser()
parser.add_argument("--duration", type=float, default=300)
parser.add_argument("--interval", type=float, default=0.2)
parser.add_argument("--rate", type=float, default=10)
parser.add_argument("--cov", type=float, default=0.1)
parser.add_argument("--tracenum", type=int, default=1)
args = parser.parse_args()

durationSec = args.duration
intervalSec = args.interval
rateMbps    = args.rate
cov         = args.cov

maxwellScale = np.sqrt(cov / (3 - 8/np.pi)) * rateMbps
maxwellLoc   = (1 - np.sqrt(8 / (3 * np.pi - 8) * cov)) * rateMbps

fdir  = "./ns-3.34/traces"

for traceid in range(args.tracenum):
    fname = "maxwell_%.1f_%.2f_%d_%d.pitree-trace" % (rateMbps, cov, durationSec, traceid)

    randomSeed = 20220223 + traceid
    randomGen = maxwell
    randomGen.random_state = np.random.Generator(np.random.PCG64(randomSeed))

    with open(os.path.join(fdir, fname), 'w') as f:
        for timestampSec in np.arange(0, durationSec, intervalSec):
            f.write("%.2fMbps 0ms 0.00\n" % (max(0, randomGen.rvs(scale=maxwellScale, loc=maxwellLoc))))

    
