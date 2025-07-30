import matplotlib
import matplotlib.pyplot as plt
import numpy as np


ns3seed = 1
seed = 1
smoothwindow = 100
smoothcollection = 500
q=1
middelay = 1
midbw = 1000
srcbw = midbw*2
sim=200
mi=500
parstring = "5_10_50_3_3_3_5_10_3_5_1_10_5"
smooththreshold = 0
mrnq=12
confseed = 0
numflow=50
cca=4
numsinksarr = [2,3,4,5]


schemearr = ["titrate","dt1"]
schemenamearr = ["Titrate","DT"]
colorarr = ["#fb8500","#9d4edd"]
markerarr = ['*','o']
arrstart = 0
arrend = 200000 
nr = 1
nc = 2
fig,axs = plt.subplots(nr,nc,figsize=(5*nc,3*nr))

longthptlist = dict()
shortthptlist = dict()
longqlenlist = dict()
shortqlenlist = dict()
with open("data_throughput_long.txt",'r') as f:
    lines = f.readlines()
    for line in lines:
        tokens = line.split()
        longthptlist[f"{tokens[0]}_{tokens[1]}"] = [float(tokens[2])]
with open("data_latency_long.txt",'r') as f:
    lines = f.readlines()
    for line in lines:
        tokens = line.split()
        longqlenlist[f"{tokens[0]}_{tokens[1]}"] = [float(tokens[2])]
with open("data_throughput_short.txt",'r') as f:
    lines = f.readlines()
    for line in lines:
        tokens = line.split()
        shortthptlist[f"{tokens[0]}_{tokens[1]}"] = [float(tokens[2])]
with open("data_latency_short.txt",'r') as f:
    lines = f.readlines()
    for line in lines:
        tokens = line.split()
        shortqlenlist[f"{tokens[0]}_{tokens[1]}"] = [float(tokens[2])]

for ischeme,scheme in enumerate(schemearr):
    longthptlist_plot = list()
    shortthptlist_plot = list()
    longqlenlist_plot = list()
    shortqlenlist_plot = list()
    for numsinks in numsinksarr:
        key = f"{numsinks}_{scheme}"
        longthptlist_plot.append(longthptlist[key])
        shortthptlist_plot.append(shortthptlist[key])
        longqlenlist_plot.append(longqlenlist[key])
        shortqlenlist_plot.append(shortqlenlist[key])
    axs[0].plot(["2","3","4","5"],longthptlist_plot,label=f"{schemenamearr[ischeme]},longRTT",color=colorarr[ischeme],marker=markerarr[1],markersize=10,zorder=(2-ischeme))
    axs[0].plot(["2","3","4","5"],shortthptlist_plot,label=f"{schemenamearr[ischeme]},shortRTT",color=colorarr[ischeme],marker=markerarr[0],markersize=10,zorder=(2-ischeme))
    axs[1].plot(["2","3","4","5"],longqlenlist_plot,color=colorarr[ischeme],marker=markerarr[1],markersize=10,zorder=(2-ischeme))
    axs[1].plot(["2","3","4","5"],shortqlenlist_plot,color=colorarr[ischeme],marker=markerarr[0],markersize=10,zorder=(2-ischeme))

axs[0].set_xlabel("# Sinks",fontsize=24)
axs[0].set_ylabel("Throughput (%)",fontsize=24)
axs[0].set_ylim(0.89,1)
axs[0].tick_params(axis='x', labelsize=20)
axs[0].tick_params(axis='y', labelsize=20)
axs[0].grid(axis='y', linestyle='--', alpha=0.7)

axs[1].set_xlabel("# Sinks",fontsize=24)
axs[1].set_ylabel("Avg Queuing\nLength (MB)",fontsize=24)
axs[1].set_ylim(0,6)
axs[1].tick_params(axis='x', labelsize=20)
axs[1].tick_params(axis='y', labelsize=20)
axs[1].grid(axis='y', linestyle='--', alpha=0.7)

fig.legend(loc="upper center",ncol=2,bbox_to_anchor=(0.5,1.33),fontsize=24)
fig.subplots_adjust(wspace=0.5)
# fig.tight_layout()
fig.savefig(f'fig11.pdf', bbox_inches='tight', dpi=500)