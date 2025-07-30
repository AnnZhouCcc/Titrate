import matplotlib
import matplotlib.pyplot as plt
import numpy as np


smoothwindow = 100
smoothcollection = 500
midbw = 1000
sim=200

confseed=0
myrtt = 300
configname = f"deltatraffic0"


schemearr = ["titrate","codel","pie","static"]
schemenamearr = ["Titrate","CoDel","PIE","Static"]
colorarr = ["#fb8500","#ffb703","#2a9d8f","#a3cef1"]
markerarr = ['*','^','v','D']
hatcharr = ["/","\\","x","-"]
arrstart = 0
arrend = 200000 

nr = 1
nc = 2
fig,axs = plt.subplots(nr,nc,figsize=(5*nc,3*nr))

thptlist = dict()
with open("data_throughput.txt",'r') as f:
    lines = f.readlines()
    for line in lines:
        tokens = line.split()
        key = f"{tokens[0]}"
        value = list()
        for i in range(1,len(tokens)):
            value.append(float(tokens[i]))
        thptlist[key] = value

qlatlist = dict()
with open("data_latency.txt",'r') as f:
    lines = f.readlines()
    for line in lines:
        tokens = line.split()
        key = f"{tokens[0]}"
        value = list()
        for i in range(1,len(tokens)):
            value.append(float(tokens[i]))
        qlatlist[key] = value

for isch,scheme in enumerate(schemearr):
    il = isch
    l = thptlist[scheme]
    if il==0: 
        zorderv=2
    else:
        zorderv=1
    axs[0].plot(["1","2","3","4"][:len(l)],l,label=schemenamearr[il],color=colorarr[il],marker=markerarr[il],markersize=10,zorder=zorderv)

axs[0].set_xlabel("Interval",fontsize=24)
axs[0].set_ylabel("Throughput (%)",fontsize=24)
axs[0].tick_params(axis='x', labelsize=20)
axs[0].tick_params(axis='y', labelsize=20)
axs[0].grid(axis='y', linestyle='--', alpha=0.7)

for isch,scheme in enumerate(schemearr):
    il = isch
    l = qlatlist[scheme]
    if il==0: 
        zorderv=2
    else:
        zorderv=1
    axs[1].plot(["1","2","3","4"][:len(l)],l,color=colorarr[il],marker=markerarr[il],markersize=10,zorder=zorderv)

axs[1].set_xlabel("Interval",fontsize=24)
axs[1].set_ylabel("Avg Queuing\nLatency (ms)",fontsize=24)
axs[1].tick_params(axis='x', labelsize=20)
axs[1].tick_params(axis='y', labelsize=20)
axs[1].grid(axis='y', linestyle='--', alpha=0.7)



fig.legend(loc="upper center",ncol=4,bbox_to_anchor=(0.5,1.2),fontsize=24)
fig.subplots_adjust(wspace=0.5)
# fig.tight_layout()
fig.savefig('fig14b.pdf', bbox_inches='tight', dpi=500)