import matplotlib
import matplotlib.pyplot as plt
import numpy as np


smoothwindow = 100
smoothcollection = 500
midbw = 1000
sim=200

ccanamearr=["cubic","bbr","realmix"]
numflownamearr=["large","small"]
rttarr=[50,300]
confseedarr = range(5)


schemearr = ["titrate","codel","pie","static"]
schemenamearr = ["Titrate","CoDel","PIE","Static"]
colorarr = ["#fb8500","#ffb703","#2a9d8f","#a3cef1"]
markerarr = ['*','^','v','+','x','D']
hatcharr = ["/","\\","x","-"]
xticklabelarr1 = list()
xticklabelarr2 = list()
for ccaname in ["Cubic","BBR","Mix"]:
    bdpname1 = "SmallBDP"
    xticklabelarr1.append(f"{ccaname}\n{bdpname1}")
    bdpname2 = "LargeBDP"
    xticklabelarr2.append(f"{ccaname}\n{bdpname2}")
xticklabelarr3 = list()
for ccaname in ["Cubic (S)","BBR (S)","Mix (S)"]:
    xticklabelarr3.append(f"{ccaname}\nLargeBDP")

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

# Parameters
n_groups = 3     # Number of groups
n_bars = len(schemearr)        # Number of bars per group
n_samples = 5    # Number of samples for each bar

# X positions for groups
x = np.arange(n_groups)

# Bar width
bar_width = 0.8 / n_bars  # Ensure bars fit in each group

# Plot
nr = 2
nc = 3
fig,axs = plt.subplots(nr,nc,figsize=(8*nc,3*nr),sharex='col')
# colors = plt.cm.tab10.colors  # Use a colormap for distinct colors

for i in range(n_bars):
    scheme = schemearr[i]
    inr=0
    for rtt in rttarr:
        for inf,numflowname in enumerate(numflownamearr):
            if numflowname=="small" and rtt==50: continue
            meanthptlist = list()
            maxthptlist = list()
            minthptlist = list()
            meanqlenlist = list()
            maxqlenlist = list()
            minqlenlist = list()
            for icca,ccaname in enumerate(ccanamearr):
                mythptlist = thptlist_wrap[f"{rtt}_{numflowname}_{ccaname}_{scheme}"]
                mythptlist = [x for x in mythptlist if x!=0]
                meanthptlist.append(sum(mythptlist)/len(mythptlist))
                minthptlist.append(min(mythptlist))
                maxthptlist.append(max(mythptlist))
                myqlenlist = qlenlist_wrap[f"{rtt}_{numflowname}_{ccaname}_{scheme}"]
                myqlenlist = [x for x in myqlenlist if x!=0]
                meanqlenlist.append(sum(myqlenlist)/len(myqlenlist))
                minqlenlist.append(min(myqlenlist))
                maxqlenlist.append(max(myqlenlist))

            # Offset the bars for each group
            if inr==0:
                axs[0,inr].bar(
                    x + i * bar_width,              # x positions
                    meanthptlist,                   # Mean values for the bars
                    bar_width,                     # Width of the bars
                    label=schemenamearr[i],            # Label for the legend
                    yerr=([a - b for a, b in zip(meanthptlist, minthptlist)],[a - b for a, b in zip(maxthptlist, meanthptlist)]),             # Error bars (std deviation)
                    capsize=4,                     # Caps on error bars
                    color=colorarr[i],
                    hatch=hatcharr[i],
                    edgecolor='grey'
                )
            else:
                axs[0,inr].bar(
                    x + i * bar_width,              # x positions
                    meanthptlist,                   # Mean values for the bars
                    bar_width,                     # Width of the bars
                    # label=schemenamearr[i],            # Label for the legend
                    yerr=([a - b for a, b in zip(meanthptlist, minthptlist)],[a - b for a, b in zip(maxthptlist, meanthptlist)]),             # Error bars (std deviation)
                    capsize=4,                     # Caps on error bars
                    color=colorarr[i],
                    hatch=hatcharr[i],
                    edgecolor='grey'
                )

            axs[1,inr].bar(
                x + i * bar_width,              # x positions
                meanqlenlist,                   # Mean values for the bars
                bar_width,                     # Width of the bars
                # label=schemenamearr[i],            # Label for the legend
                yerr=([a - b for a, b in zip(meanqlenlist, minqlenlist)],[a - b for a, b in zip(maxqlenlist, meanqlenlist)]),             # Error bars (std deviation)
                capsize=4,                     # Caps on error bars
                color=colorarr[i],
                hatch=hatcharr[i],
                edgecolor='grey'
            )
            inr+=1

axs[0,0].set_ylabel('Throughput (%)',fontsize=24)
axs[1,0].set_ylabel('Avg Queuing\nLatency (ms)',fontsize=24)
axs[1,0].set_xticklabels(xticklabelarr1,fontsize=24)
axs[1,1].set_xticklabels(xticklabelarr2,fontsize=24)
axs[1,2].set_xticklabels(xticklabelarr3,fontsize=24)

for inr in [0,1,2]:
    # Add labels, title, and legend
    axs[0,inr].set_xticks(x + bar_width * (n_bars / 2 - 0.5))  # Center group labels
    axs[0,inr].grid(axis='y', linestyle='--', alpha=0.7)
    axs[0,inr].tick_params(axis='y', labelsize=20)
    axs[0,inr].set_ylim(0.6,1.05)
    axs[1,inr].set_xticks(x + bar_width * (n_bars / 2 - 0.5))  # Center group labels
    axs[1,inr].grid(axis='y', linestyle='--', alpha=0.7)
    axs[1,inr].tick_params(axis='y', labelsize=20)

axs[1,2].set_ylim(0,300)

fig.legend(loc="upper center",ncol=6,bbox_to_anchor=(0.5,1.12),fontsize=24)
plt.tight_layout()
plt.savefig(f'fig8.pdf', bbox_inches='tight', dpi=500)