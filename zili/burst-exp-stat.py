import itertools
import numpy as np
import subprocess as sp

burstlist = [15000]
flownumlist = [50]
tracenum = 50
baselinelist = [ #"FqCoDel2ms_0", "FqCoDel5ms_0", "FqCoDel10ms_0", "FqCoDel20ms_0", "FqCoDel50ms_0", 
    "FqCoDel5000ms10_0", "FqCoDel5000ms20_0", "FqCoDel5000ms30_0", "FqCoDel5000ms60_0", 
    "FqCoDel5000ms120_0", "FqCoDel5000ms250_0", "FqCoDel5000ms500_0", "FqCoDel5000ms1000_0",
    "PfifoFast10_0", "PfifoFast20_0", "PfifoFast30_0", "PfifoFast60_0", 
    "PfifoFast120_0", "PfifoFast250_0", "PfifoFast500_0", "PfifoFast1000_0",
    "PfifoFast10_1", "PfifoFast20_1", "PfifoFast30_1", "PfifoFast60_1", 
    "PfifoFast120_1", "PfifoFast250_1", "PfifoFast500_1", "PfifoFast1000_1"] #,
    # "Dwrr0.125000_1", "Dwrr0.167000_1", "Dwrr0.250000_1", "Dwrr0.333000_1", "Dwrr0.500000_1", 
    # "Dwrr0.707000_1", "Dwrr1.000000_1", "Dwrr1.414000_1", "Dwrr2.000000_1", "Dwrr3.000000_1", 
    # "Dwrr4.000000_1", "Dwrr6.000000_1", "Dwrr8.000000_1"]
avgfct = np.zeros((len(baselinelist), len(burstlist), len(flownumlist)))
p90fct = np.zeros((len(baselinelist), len(burstlist), len(flownumlist)))
maxrtt = np.zeros((len(baselinelist), len(burstlist), len(flownumlist)))
rttdur = np.zeros((len(baselinelist), len(burstlist), len(flownumlist)))
maxapp = np.zeros((len(baselinelist), len(burstlist), len(flownumlist)))
rtonum = np.zeros((len(baselinelist), len(burstlist), len(flownumlist)))

indir = "ns-3.34/logs/"
outdir = "ns-3.34/results/"

for baselineidx in range(len(baselinelist)):
    for burstidx in range(len(burstlist)):
        for flowidx in range(len(flownumlist)):
            burst = burstlist[burstidx]
            flownum = flownumlist[flowidx]
            algorithm = baselinelist[baselineidx]
            avgFctTrace = []
            p90FctTrace = []
            rttDurTrace = []
            maxRttTrace = []
            maxAppTrace = []
            rtoNumTrace = 0
            for traceid in range(tracenum):
                trace = "maxwell_10.0_0.02_%d.pitree-trace" % traceid
                isRto = int(sp.check_output("awk -f isrto.awk "+ indir + "bwCopa_" + algorithm + "_" + trace + "_" + str(flownum) + "_1_" + str(burst) + ".tr", shell=True).decode('utf-8').split()[0])
                if isRto:
                    rtoNumTrace += 1
                else:
                    avgFctTrace.append(float(sp.check_output("awk -f statfct.awk " + indir + "fctCopa_" + algorithm + "_" + trace + "_" + str(flownum) + "_1_" + str(burst) + ".tr | awk '{sum+=$2} END{print sum/NR/1000000}'", shell=True).decode('utf-8').split()[0]))
                    p90FctTrace.append(float(sp.check_output("awk -f statfct.awk " + indir + "fctCopa_" + algorithm + "_" + trace + "_" + str(flownum) + "_1_" + str(burst) + ".tr | sort -nk2 | awk '{all[NR] = $2} END{print all[int(NR*0.9)]/1000000}'", shell=True).decode('utf-8').split()[0]))
                    maxRttTrace.append(int(sp.check_output("awk -f perflow-res.awk " + indir + "bwCopa_" + algorithm + "_" + trace + "_" + str(flownum) + "_1_" + str(burst) + ".tr | awk '{print $4}'", shell=True).decode('utf-8').split()[0]))
                    rttDurTrace.append(int(sp.check_output("awk -f perflow-res.awk " + indir + "bwCopa_" + algorithm + "_" + trace + "_" + str(flownum) + "_1_" + str(burst) + ".tr | awk '{print $5}'", shell=True).decode('utf-8').split()[0]))
                    maxAppTrace.append(int(sp.check_output("awk -f statapp.awk " + indir + "appCopa_" + algorithm + "_" + trace + "_" + str(flownum) + "_1_" + str(burst) + ".tr | sort -nrk2 | awk 'NR==1{print $2}'", shell=True).decode('utf-8').split()[0]))

            avgfct[baselineidx, burstidx, flowidx] = 0 if np.isnan(np.mean(avgFctTrace)) else np.mean(avgFctTrace)
            p90fct[baselineidx, burstidx, flowidx] = 0 if np.isnan(np.mean(p90FctTrace)) else np.mean(p90FctTrace)
            maxrtt[baselineidx, burstidx, flowidx] = 0 if np.isnan(np.mean(maxRttTrace)) else np.mean(maxRttTrace)
            rttdur[baselineidx, burstidx, flowidx] = 0 if np.isnan(np.mean(rttDurTrace)) else np.mean(rttDurTrace)
            maxapp[baselineidx, burstidx, flowidx] = 0 if np.isnan(np.mean(maxAppTrace)) else np.mean(maxAppTrace)
            rtonum[baselineidx, burstidx, flowidx] = rtoNumTrace / tracenum

for baselineidx in range(len(baselinelist)):
    np.savetxt(outdir + 'avgfct_%s_heatmap.result' % baselinelist[baselineidx], avgfct[baselineidx], fmt="%d")
    np.savetxt(outdir + 'p90fct_%s_heatmap.result' % baselinelist[baselineidx], p90fct[baselineidx], fmt="%d")
    np.savetxt(outdir + 'maxrtt_%s_heatmap.result' % baselinelist[baselineidx], maxrtt[baselineidx], fmt="%d")
    np.savetxt(outdir + 'rttdur_%s_heatmap.result' % baselinelist[baselineidx], rttdur[baselineidx], fmt="%d")
    np.savetxt(outdir + 'maxapp_%s_heatmap.result' % baselinelist[baselineidx], maxapp[baselineidx], fmt="%d")

for burstidx in range(len(burstlist)):
    np.savetxt(outdir + 'avgfct_%d_burst.result' % burstlist[burstidx], np.concatenate((np.array(flownumlist)[:, None].T - 1, avgfct[:, burstidx, :])).T, fmt="%d")
    np.savetxt(outdir + 'p90fct_%d_burst.result' % burstlist[burstidx], np.concatenate((np.array(flownumlist)[:, None].T - 1, p90fct[:, burstidx, :])).T, fmt="%d")
    np.savetxt(outdir + 'maxrtt_%d_burst.result' % burstlist[burstidx], np.concatenate((np.array(flownumlist)[:, None].T - 1, maxrtt[:, burstidx, :])).T, fmt="%d")
    np.savetxt(outdir + 'rttdur_%d_burst.result' % burstlist[burstidx], np.concatenate((np.array(flownumlist)[:, None].T - 1, rttdur[:, burstidx, :])).T, fmt="%d")
    np.savetxt(outdir + 'maxapp_%d_burst.result' % burstlist[burstidx], np.concatenate((np.array(flownumlist)[:, None].T - 1, maxapp[:, burstidx, :])).T, fmt="%d")

for flownumidx in range(len(flownumlist)):
    np.savetxt(outdir + 'avgfct_%d_flownum.result' % flownumlist[flownumidx], np.concatenate((np.array(burstlist)[:, None].T, avgfct[:, :, flownumidx])).T, fmt="%d")
    np.savetxt(outdir + 'p90fct_%d_flownum.result' % flownumlist[flownumidx], np.concatenate((np.array(burstlist)[:, None].T, p90fct[:, :, flownumidx])).T, fmt="%d")
    np.savetxt(outdir + 'maxrtt_%d_flownum.result' % flownumlist[flownumidx], np.concatenate((np.array(burstlist)[:, None].T, maxrtt[:, :, flownumidx])).T, fmt="%d")
    np.savetxt(outdir + 'rttdur_%d_flownum.result' % flownumlist[flownumidx], np.concatenate((np.array(burstlist)[:, None].T, rttdur[:, :, flownumidx])).T, fmt="%d")
    np.savetxt(outdir + 'maxapp_%d_flownum.result' % flownumlist[flownumidx], np.concatenate((np.array(burstlist)[:, None].T, maxapp[:, :, flownumidx])).T, fmt="%d")

for burstidx, flownumidx in itertools.product(range(len(burstlist)), range(len(flownumlist))):
    np.savetxt(outdir + 'motiv_%d_%d_burst_flownum.result' % (burstlist[burstidx], flownumlist[flownumidx]), 
        np.array([maxapp[:, burstidx, flownumidx], avgfct[:, burstidx, flownumidx]]).T, fmt="%d %.3f")
    