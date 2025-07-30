##################
# Helper methods #
##################

import random
import os
def load_stats(dir,q=1,numqueues=2,numports=2,p=0):
    timelist = list()
    bufferpctlist = list()
    qlenlist = list()
    sentlist = list()
    thptlist = list()
    droplist = list()
    threslist = list()
    for i in range(numports * numqueues):
        qlenlist.append(list())
        sentlist.append(list())
        thptlist.append(list())
        droplist.append(list())
        threslist.append(list())

    with open(dir+"tor.tr", 'r') as f:
        lines = f.readlines()[1:-1]
        for line in lines:
            if line.startswith("\x00"): continue
            tokens = line.split()
            timestamp = int(tokens[0])
            buffer = float(tokens[2])
            for i in range(numports * numqueues):
                qlenlist[i].append(int(tokens[3+i*5]))
            for i in range(numports * numqueues):
                thptlist[i].append(float(tokens[3+i*5+1]))
            for i in range(numports * numqueues):
                sentlist[i].append(int(tokens[3+i*5+2]))
            for i in range(numports * numqueues):
                droplist[i].append(int(tokens[3+i*5+3]))
            for i in range(numports * numqueues):
                threslist[i].append(int(tokens[3+i*5+4]))
            timelist.append(timestamp)
            bufferpctlist.append(buffer)
    
    return [timelist,qlenlist[p*numqueues+q],sentlist[p*numqueues+q],thptlist[p*numqueues+q],droplist[p*numqueues+q],threslist[p*numqueues+q]]


import matplotlib
import matplotlib.pyplot as plt
import numpy as np
# import pandas as pd




######################
# Setting parameters #
######################

from pathlib import Path
script_path = Path(__file__).resolve()
mydir = script_path.parent


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




# ###################
# # Processing data #
# ###################

datadict = dict()
for numsinks in numsinksarr:
    configname = f"multirtt_numsinks{numsinks}"
    conffile = f"../detailed_ae/multirtt/configurations/{configname}.conf"
    
    totalbuffer = 10000000+1000000*(numsinks-1)
    startbuffer = int(totalbuffer//10)
    targetbw = int(totalbuffer//10)

    middelaystr=f"{middelay}"
    midbwstr=f"{midbw}"
    srcbwstr=f"{srcbw}"
    for i in range(numsinks-1):
        middelaystr+=f"_{middelay}"
        midbwstr+=f"_{midbw}"
        srcbwstr+=f"_{srcbw}"

    datadict[configname] = dict()
    datadict[configname]["titrate"] = dict()
    datadict[configname]["dt1"] = dict()
    datadict[configname]["pie"] = dict()
    datadict[configname]["static"] = dict()

    # Titrate
    pawmode="paw"
    qdisctype="Fifo"
    bufferalg="111"
    confstr = f"--targetBW={targetbw} --mainRoomNumQueues={mrnq}"
    logstr = f"{totalbuffer}_{bufferalg}_0_204_{mrnq}_{startbuffer}/{srcbwstr}_{targetbw}"
    fdir = f"{mydir}/../../ns-3.34/logs/{configname}_{pawmode}/{logstr}/0/1_{mi}_2_4/{parstring}/{smoothcollection}_{smoothwindow}_{smooththreshold}/{ns3seed}/"
    if not os.path.exists(fdir+"tor.tr"):
        print(f"***Warning: {fdir}tor.tr not found")
    else:
        print(f"{fdir}tor.tr")
        for port in range(numsinks):
            statslist = load_stats(fdir,q=1,numqueues=2,numports=numsinks,p=port)
            if len(statslist[0])<sim*1000: print(f"***Short logs: {configname}, totalbuffer={totalbuffer}, srcbwstr={srcbwstr}, {len(statslist[0])}")
            datadict[configname]["titrate"][port] = statslist

    # DT1
    pawmode="pa"
    qdisctype="Fifo"
    bufferalg="101"
    alpha=1
    alphastr=f"{alpha}"
    for i in range(numsinks-1):
        alphastr+=f"_{alpha}"
    confstr = f"--alphaString={alphastr} --targetBW={targetbw}"
    logstr = f"{totalbuffer}_{bufferalg}_{alphastr}_204_1_{startbuffer}/{srcbwstr}_{targetbw}"
    fdir = f"{mydir}/../../ns-3.34/logs/{configname}_{pawmode}/{logstr}/0/1_{mi}_2_4/{parstring}/{smoothcollection}_{smoothwindow}_{smooththreshold}/{ns3seed}/"
    if not os.path.exists(fdir+"tor.tr"):
        print(f"***Warning: {fdir}tor.tr not found")
    else:
        print(f"{fdir}tor.tr")
        for port in range(numsinks):
            statslist = load_stats(fdir,q=1,numqueues=2,numports=numsinks,p=port)
            if len(statslist[0])<sim*1000: print(f"***Short logs: {configname}, totalbuffer={totalbuffer}, srcbwstr={srcbwstr}, {len(statslist[0])}")
            datadict[configname]["dt1"][port] = statslist


schemearr = ["titrate","dt1"]
arrstart = 0
arrend = 200000 

longthptlist = dict()
shortthptlist = dict()
longqlenlist = dict()
shortqlenlist = dict()
for scheme in schemearr:
    for numsinks in numsinksarr:
        configname = f"multirtt_numsinks{numsinks}"
        key = f"{numsinks}_{scheme}"

        port = 0
        datator = datadict[configname][scheme][port]
        time = datator[0][arrstart:arrend]
        qlen = datator[1][arrstart:arrend]
        sent = datator[2][arrstart:arrend]
        thpt = datator[3][arrstart:arrend]
        drop = datator[4][arrstart:arrend]
        thres = datator[5][arrstart:arrend]
        longthptlist[key] = (sum(thpt)/len(thpt))
        longqlenlist[key] = (sum(qlen)/len(qlen)/1000000)
        
        myshortthptlist = list()
        myshortqlenlist = list()
        for port in range(1,numsinks):
            datator = datadict[configname][scheme][port]
            time = datator[0][arrstart:arrend]
            qlen = datator[1][arrstart:arrend]
            sent = datator[2][arrstart:arrend]
            thpt = datator[3][arrstart:arrend]
            drop = datator[4][arrstart:arrend]
            thres = datator[5][arrstart:arrend]
            myshortthptlist.append(sum(thpt)/len(thpt))
            myshortqlenlist.append(sum(qlen)/len(qlen)/1000000)
        shortthptlist[key] = (sum(myshortthptlist)/len(myshortthptlist))
        shortqlenlist[key] = (sum(myshortqlenlist)/len(myshortqlenlist))


def convert_list_to_str(mylist):
    mystr = ""
    for x in mylist:
        mystr+=f"{x} "
    return mystr

with open('data_throughput_long.txt', 'w') as f:
    for scheme in schemearr:
        for numsinks in numsinksarr:
            key = f"{numsinks}_{scheme}"
            f.write(f"{numsinks} {scheme} {longthptlist[key]}\n")
with open('data_latency_long.txt', 'w') as f:
    for scheme in schemearr:
        for numsinks in numsinksarr:
            key = f"{numsinks}_{scheme}"
            f.write(f"{numsinks} {scheme} {longqlenlist[key]}\n")
with open('data_throughput_short.txt', 'w') as f:
    for scheme in schemearr:
        for numsinks in numsinksarr:
            key = f"{numsinks}_{scheme}"
            f.write(f"{numsinks} {scheme} {shortthptlist[key]}\n")
with open('data_latency_short.txt', 'w') as f:
    for scheme in schemearr:
        for numsinks in numsinksarr:
            key = f"{numsinks}_{scheme}"
            f.write(f"{numsinks} {scheme} {shortqlenlist[key]}\n")