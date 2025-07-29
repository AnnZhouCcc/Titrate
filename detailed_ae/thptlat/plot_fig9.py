import matplotlib
import matplotlib.pyplot as plt
import numpy as np


smoothwindow = 100
smoothcollection = 500
midbw = 1000
sim=200

ccanamearr=["cubic"]
numflownamearr=["small"]
rttarr=[50]
confseedarr = range(5)


schemearr = ["titrate","codel","pie","static"]
schemenamearr = ["Titrate","CoDel","PIE","Static"]
colorarr = ["#fb8500","#ffb703","#2a9d8f","#a3cef1"]
markerarr = ['*','^','v','+','x','D']
hatcharr = ["/","\\","x","-"]
# xticklabelarr = ["Cubic","BBR","Mix"]

schemearr = ["titrate","codel","pie","static"]
arrstart = 0
arrend = 200000

thptlist_wrap = dict()
qlenlist_wrap = dict()
with open("data_throughput.txt",'r') as f:
    lines = f.readlines()
    for line in lines:
        tokens = line.split()
        thptlist_wrap[f"{tokens[0]}_{tokens[2]}_{tokens[1]}_{tokens[3]}"] = [float(tokens[4]),float(tokens[5]),float(tokens[6]),float(tokens[7]),float(tokens[8])]
with open("data_latency.txt",'r') as f:
    lines = f.readlines()
    for line in lines:
        tokens = line.split()
        qlenlist_wrap[f"{tokens[0]}_{tokens[2]}_{tokens[1]}_{tokens[3]}"] = [float(tokens[4]),float(tokens[5]),float(tokens[6]),float(tokens[7]),float(tokens[8])]

nr = 1
nc = 2
fig,axs = plt.subplots(nr,nc,figsize=(5*nc,3*nr))

ccaname = ccanamearr[0]
rtt = rttarr[0]
numflowname = numflownamearr[0]

thptlist = [thptlist_wrap[f"{rtt}_{numflowname}_{ccaname}_{scheme}"] for scheme in schemearr]
qlatlist = [qlenlist_wrap[f"{rtt}_{numflowname}_{ccaname}_{scheme}"] for scheme in schemearr]


cthptlist = list()
for l in thptlist:
    cthptlist.append(sum(l)/len(l))
lowererr = list()
uppererr = list()
for il,l in enumerate(thptlist):
    lowererr.append(cthptlist[il]-min(l))
    uppererr.append(max(l)-cthptlist[il])
axs[0].bar(schemenamearr,cthptlist,yerr=[lowererr,uppererr],width=0.4,capsize=5,color=colorarr,hatch=hatcharr,edgecolor='grey')


cqlatlist = list()
for l in qlatlist:
    cqlatlist.append(sum(l)/len(l))
lowererr = list()
uppererr = list()
for il,l in enumerate(qlatlist):
    lowererr.append(cqlatlist[il]-min(l))
    uppererr.append(max(l)-cqlatlist[il])
axs[1].bar(schemenamearr,cqlatlist,yerr=[lowererr,uppererr],width=0.4,capsize=5,color=colorarr,hatch=hatcharr,edgecolor='grey')

axs[0].set_ylabel('Throughput (%)',fontsize=24)
axs[1].set_ylabel('Avg Queuing\nLatency (ms)',fontsize=24)

for i in [0,1]:
    axs[i].set_xticklabels(schemenamearr,fontsize=24)
    axs[i].tick_params(axis='x', labelsize=20)
    axs[i].tick_params(axis='y', labelsize=20)
    axs[i].grid(axis='y', linestyle='--', alpha=0.7)
    
axs[0].set_ylim(0.6,None)
# axs[1].set_yticks([0,50,100,150,200,1000,1100],[0,50,100,150,200,1000,1100])


# fig.legend(loc="upper center",ncol=6,bbox_to_anchor=(0.5,1.12),fontsize=24)
plt.tight_layout()
plt.savefig(f'fig9.pdf', bbox_inches='tight', dpi=500)