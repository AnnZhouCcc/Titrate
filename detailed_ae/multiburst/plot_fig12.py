import matplotlib
import matplotlib.pyplot as plt
import numpy as np


ns3seed = 1
seed = 1
smoothwindow = 600
smoothcollection = 500
q=1
middelay = 1
midbw = 1000
srcbw = midbw*2
sim=150
mi=500
parstring = "5_10_50_3_3_3_5_10_3_5_1_10_5"
smooththreshold = 0
mrnq=12
confseed = 0
numflow=50
cca=4
rtt=10
numsinks = 2
middelaystr=f"{middelay}"
midbwstr=f"{midbw}"
srcbwstr=f"{srcbw}"
for i in range(numsinks-1):
    middelaystr+=f"_{middelay}"
    midbwstr+=f"_{midbw}"
    srcbwstr+=f"_{srcbw}"
totalbufferarr = [12000000,16000000,20000000,24000000]

webtraceidlist = [967, 652, 959, 35, 521, 707, 702, 547, 795, 461]
numflowinburst = 1
appstartarr = range(20,120,10)
srclinkrate = 2


schemearr = ["titrate","dt1"]
numwebtraces = len(webtraceidlist)
schemenamearr = ["Titrate","DT"]
colorarr = ["#fb8500","#9d4edd"]
markerarr = ['*','o']
arrstart = 0
arrend = 200000 
nr = 2
nc = 2
fig,axs = plt.subplots(nr,nc,figsize=(5*nc,3*nr),sharex=True)

thptlist = dict()
qlenlist = dict()
wwwtdroplist = dict()
wwwtdurationlist = dict()
with open("data_throughput.txt",'r') as f:
    lines = f.readlines()
    for line in lines:
        tokens = line.split()
        thptlist[f"{tokens[0]}_{tokens[1]}"] = [float(tokens[2])]
with open("data_latency.txt",'r') as f:
    lines = f.readlines()
    for line in lines:
        tokens = line.split()
        qlenlist[f"{tokens[0]}_{tokens[1]}"] = [float(tokens[2])]
with open("data_ndrop.txt",'r') as f:
    lines = f.readlines()
    for line in lines:
        tokens = line.split()
        wwwtdroplist[f"{tokens[0]}_{tokens[1]}"] = [float(tokens[2])]
with open("data_bct.txt",'r') as f:
    lines = f.readlines()
    for line in lines:
        tokens = line.split()
        wwwtdurationlist[f"{tokens[0]}_{tokens[1]}"] = [float(tokens[2])]

for ischeme,scheme in enumerate(schemearr):
    thptlist_plot = list()
    qlenlist_plot = list()
    wwwtdroplist_plot = list()
    wwwtdurationlist_plot = list()
    for totalbuffer in totalbufferarr:
        key = f"{totalbuffer}_{scheme}"
        thptlist_plot.append(thptlist[key])
        qlenlist_plot.append(qlenlist[key])
        wwwtdroplist_plot.append(wwwtdroplist[key])
        wwwtdurationlist_plot.append(wwwtdurationlist[key])
    axs[1,0].plot([str(int(x/1000000)) for x in totalbufferarr],thptlist_plot,label=f"{schemenamearr[ischeme]}",color=colorarr[ischeme],marker=markerarr[ischeme],markersize=10,zorder=(2-ischeme))
    axs[1,1].plot([str(int(x/1000000)) for x in totalbufferarr],qlenlist_plot,color=colorarr[ischeme],marker=markerarr[ischeme],markersize=10,zorder=(2-ischeme))
    axs[0,0].plot([str(int(x/1000000)) for x in totalbufferarr],wwwtdroplist_plot,color=colorarr[ischeme],marker=markerarr[ischeme],markersize=10,zorder=(2-ischeme))
    axs[0,1].plot([str(int(x/1000000)) for x in totalbufferarr],wwwtdurationlist_plot,color=colorarr[ischeme],marker=markerarr[ischeme],markersize=10,zorder=(2-ischeme))

axs[0,0].set_ylabel("# Drops\nin Burst",fontsize=24)
axs[0,0].tick_params(axis='x', labelsize=20)
axs[0,0].tick_params(axis='y', labelsize=20)
axs[0,0].grid(axis='y', linestyle='--', alpha=0.7)

axs[0,1].set_ylabel("Burst Compl\nTime (ms)",fontsize=24)
axs[0,1].set_ylim(0,850)
axs[0,1].set_yticks([0, 300, 600, 900])
axs[0,1].tick_params(axis='x', labelsize=20)
axs[0,1].tick_params(axis='y', labelsize=20)
axs[0,1].grid(axis='y', linestyle='--', alpha=0.7)

axs[1,0].set_xlabel("Total Buffer (MB)",fontsize=24)
axs[1,0].set_ylabel("Throughput (%)",fontsize=24)
axs[1,0].set_ylim(0.96,1.01)
axs[1,0].tick_params(axis='y', labelsize=20)
axs[1,0].tick_params(axis='x', labelsize=20)
axs[1,0].grid(axis='y', linestyle='--', alpha=0.7)

axs[1,1].set_xlabel("Total Buffer (MB)",fontsize=24)
axs[1,1].set_ylabel("Avg Queuing\nLength (MB)",fontsize=24)
axs[1,1].set_ylim(0,None)
axs[1,1].set_yticks([0, 3, 6, 9, 12])
axs[1,1].tick_params(axis='y', labelsize=20)
axs[1,1].tick_params(axis='x', labelsize=20)
axs[1,1].grid(axis='y', linestyle='--', alpha=0.7)


fig.legend(loc="upper center",ncol=2,bbox_to_anchor=(0.5,1.03),fontsize=24)
fig.subplots_adjust(wspace=0.5)
# fig.tight_layout()
fig.savefig('fig12.pdf', bbox_inches='tight', dpi=500)