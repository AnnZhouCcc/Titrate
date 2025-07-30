import matplotlib
import matplotlib.pyplot as plt
import numpy as np


smoothwindow = 100
smoothcollection = 500
smooththreshold = 0
midbw = 1000
sim=200

ccaname="realmix"
numflowname="large"
rtt=300
confseed=0
configname = f"cca{ccaname}_nf{numflowname}_rtt{rtt}_cseed{confseed}"
dbwname = "conf0"


schemearr = ["titrate","codel","pie","static"]
schemenamearr = ["Titrate","CoDel","PIE","Static"]
textarr = ["1000Mbps","500Mpbs","750Mbps","250Mbps"]
colorarr = ["#fb8500","#ffb703","#2a9d8f","#a3cef1"]
arrstart = 0
arrend = 200000 

nr = 2
nc = 1
fig,axs = plt.subplots(nr,nc,figsize=(12*nc,3.5*nr),sharex=True,sharey=True)

timeserieslist = dict()
with open("data_timeseries.txt",'r') as f:
    lines = f.readlines()
    for line in lines:
        tokens = line.split()
        key = f"{tokens[0]}_{tokens[1]}"
        value = list()
        for i in range(2,len(tokens)):
            value.append(float(tokens[i]))
        timeserieslist[key] = value

for isch,scheme in enumerate(schemearr):
    if scheme=="titrate":
        alphav=1
        axs[0].plot(timeserieslist["titrate_time"],timeserieslist["titrate_qlen"],label=schemenamearr[isch],color=colorarr[isch],alpha=alphav)
        axs[0].plot(timeserieslist["titrate_time"],timeserieslist["titrate_thres"],label="Titrate (Threshold)",color="#9d4edd",alpha=alphav)
    else:
        alphav = 0.8
        if scheme=="pie": alphav=0.5
        axs[1].plot(timeserieslist[f"{scheme}_time"],timeserieslist[f"{scheme}_qlen"],label=schemenamearr[isch],color=colorarr[isch],alpha=alphav)

for iaxs in [0,1]:
    for it,t in enumerate(range(0, 201, 50)):
        axs[iaxs].axvline(x=t, color='gray', linestyle='--', linewidth=3)

        if t < 200:
            midpoint = t + 25
            axs[iaxs].text(midpoint, 68, textarr[it], ha='center', va='bottom', fontsize=24, color='gray')

axs[1].set_xlabel("Time (s)",fontsize=24)
for iaxs in [0,1]:
    axs[iaxs].set_ylabel("Queue Len (MB)",fontsize=24)
    axs[iaxs].tick_params(axis='x', labelsize=20)
    axs[iaxs].tick_params(axis='y', labelsize=20)
    axs[iaxs].grid(axis='y', linestyle='--', alpha=0.7)
fig.legend(loc="upper center",ncol=6,bbox_to_anchor=(0.5,1.1),fontsize=24)
fig.tight_layout()
fig.savefig('fig13a.pdf', bbox_inches='tight', dpi=500)