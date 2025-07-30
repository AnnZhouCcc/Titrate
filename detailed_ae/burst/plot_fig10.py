import matplotlib
import matplotlib.pyplot as plt
import numpy as np


webtraceidlist = [26,141]

ns3seed = 1
numsinks=1
seed = 1
smoothcollection = 500
q=1
middelay = 1
midbw = 1000
srcbw = midbw*2
middelaystr=f"{middelay}"
srcbwstr=f"{srcbw}"
midbwstr=f"{midbw}"
mi=500
parstring = "5_10_50_3_3_3_5_10_3_5_1_10_5"
smooththreshold = 0
mrnq=12

ccanamearr=["realmix"]
rttarr=[300]
numflownamearr=["small"]
smoothwindow = 100
confseedarr = range(10)

numwebtraces = 1
numflowinburst = 1
srclinkrate = 2
sim=20


schemearr = ["titrate","p","codel","pie","static"]
schemenamearr = ["Titrate","Titrate (No EQlen)","CoDel","PIE","Static"]
colorarr = ["#fb8500","#9d4edd","#ffb703","#2a9d8f","#a3cef1"]
markerarr = ['*','o','^','v','D']
arrstart = 0 
arrend = 20000
nr = 1
nc = 2
fig,axs = plt.subplots(nr,nc,figsize=(5*nc,3*nr))

bct_sorted_dict = dict()
with open("data.txt",'r') as f:
    lines = f.readlines()
    for line in lines:
        tokens = line.split()
        bct_sorted_dict[f"{tokens[0]}_{tokens[1]}"] = [float(tokens[2]),float(tokens[3]),float(tokens[4]),float(tokens[5]),float(tokens[6]),float(tokens[7]),float(tokens[8]),float(tokens[9]),float(tokens[10]),float(tokens[11])]

ccaname = ccanamearr[0]
numflowname = numflownamearr[0]
rtt = rttarr[0]
for ischeme,scheme in enumerate(schemearr):
    for iw,wtid in enumerate(webtraceidlist):
        sorted_wwwtdurationlist = bct_sorted_dict[f"{wtid}_{scheme}"]
        cdf = np.arange(1, len(sorted_wwwtdurationlist) + 1) / len(sorted_wwwtdurationlist)
        if iw==0:
            axs[iw].plot(sorted_wwwtdurationlist,cdf,label=schemenamearr[ischeme],marker='.',markersize=10,alpha=0.7,color=colorarr[ischeme],zorder=(4-ischeme))
        else:
            axs[iw].plot(sorted_wwwtdurationlist,cdf,marker='.',markersize=10,alpha=0.7,color=colorarr[ischeme],zorder=(4-ischeme))

axs[0].set_title("Smaller Burst",fontsize=24)
axs[1].set_title("Larger Burst",fontsize=24)
axs[0].set_ylabel("CDF (%)",fontsize=20)
axs[0].set_xlabel("Burst Completion Time (s)",fontsize=20)
axs[1].set_xlabel("Burst Completion Time (s)",fontsize=20)

for i in [0,1]:
    axs[i].set_ylim(0,1.05)
    axs[i].tick_params(axis='x', labelsize=20)
    axs[i].tick_params(axis='y', labelsize=20)
    axs[i].grid(axis='y', linestyle='--', alpha=0.7)
    axs[i].set_yticks([0,0.2,0.4,0.6,0.8,1])

fig.legend(loc="upper center",ncol=3,bbox_to_anchor=(0.5,1.42),fontsize=24)
# fig.tight_layout()
fig.savefig('fig10.pdf', bbox_inches='tight', dpi=500)