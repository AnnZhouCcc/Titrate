##################
# Helper methods #
##################

import random
import pickle
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
numsinks=1
seed = 1
smoothwindow = 100
smoothcollection = 500
q=1
middelay = 1
midbw = 1000
srcbw = midbw*2
middelaystr=f"{middelay}"
srcbwstr=f"{srcbw}"
midbwstr=f"{midbw}"
sim=200
mi=500
parstring = "5_10_50_3_3_3_5_10_3_5_1_10_5"
smooththreshold = 0
ccanamearr=["cubic","bbr","realmix"]
numflownamearr=["small","large"]
rttarr=[50,300]
mrnq=12
confseedarr = range(5)




# ###################
# # Processing data #
# ###################

datadict = dict()
for cca in ccanamearr:
    for numflow in numflownamearr:
        for rtt in rttarr:
            for confseed in confseedarr:
                configname = f"cca{cca}_nf{numflow}_rtt{rtt}_cseed{confseed}"
                datadict[configname] = dict()
                datadict[configname]["titrate"] = dict()
                datadict[configname]["codel"] = dict()
                datadict[configname]["pie"] = dict()
                datadict[configname]["static"] = dict()

                # Titrate
                if cca!="bbr" and numflow=="small" and rtt>50:
                    totalbuffer = 20000000
                    startbuffer = totalbuffer
                    targetbw = int(totalbuffer//10)
                else:
                    BDP = int(rtt/1000 * midbw * 1000000/8)
                    totalbuffer = 2*BDP
                    startbuffer = totalbuffer
                    targetbw = int(totalbuffer//10)
                pawmode="pa"
                qdisctype="Fifo"
                bufferalg="111"
                confstr = f"--targetBW={targetbw} --mainRoomNumQueues={mrnq}"
                logstr = f"{totalbuffer}_{bufferalg}_0_204_{mrnq}_{startbuffer}/{srcbwstr}_{targetbw}"
                fdir = f"{mydir}/../../ns-3.34/logs/{configname}_{pawmode}/{logstr}/0/1_{mi}_2_4/{parstring}/{smoothcollection}_{smoothwindow}_{smooththreshold}/{ns3seed}/"
                if not os.path.exists(fdir+"tor.tr"):
                    print(f"***Warning: {fdir}tor.tr not found")
                else:
                    print(f"{fdir}tor.tr")
                    statslist = load_stats(fdir,q=1,numqueues=2,numports=numsinks,p=0)
                    if len(statslist[0])<sim*1000: print(f"***Short logs: {configname}, totalbuffer={totalbuffer}, srcbwstr={srcbwstr}, {len(statslist[0])}")
                    datadict[configname]["titrate"] = statslist

                BDP = int(rtt/1000 * midbw * 1000000/8)
                totalbuffer = 2*BDP
                startbuffer = totalbuffer
                targetbw = int(totalbuffer//10)
                
                # CoDel
                pawmode="fixed"
                qdisctype="CoDel"
                bufferalg="101"
                alpha=8
                confstr = f"--codelTarget=5ms --codelInterval=100ms --alphaString={alpha} --targetBW={targetbw}"
                logstr = f"codel_5ms_100ms/{totalbuffer}_{bufferalg}_{alpha}_204_1_{startbuffer}/{srcbwstr}_{targetbw}"
                fdir = f"{mydir}/../../ns-3.34/logs/{configname}_{pawmode}/{logstr}/0/1_{mi}_2_4/{parstring}/{smoothcollection}_{smoothwindow}_{smooththreshold}/{ns3seed}/"
                if not os.path.exists(fdir+"tor.tr"):
                    print(f"***Warning: {fdir}tor.tr not found")
                else:
                    print(f"{fdir}tor.tr")
                    statslist = load_stats(fdir,q=1,numqueues=2,numports=numsinks,p=0)
                    if len(statslist[0])<sim*1000: print(f"***Short logs: {configname}, totalbuffer={totalbuffer}, srcbwstr={srcbwstr}, {len(statslist[0])}")
                    datadict[configname]["codel"] = statslist

                # PIE
                pawmode="fixed"
                qdisctype="Pie"
                bufferalg="101"
                alpha=8
                confstr = f"--pieTarget=20ms --alphaString={alpha} --targetBW={targetbw}"
                logstr = f"pie_20ms/{totalbuffer}_{bufferalg}_{alpha}_204_1_{startbuffer}/{srcbwstr}_{targetbw}"
                fdir = f"{mydir}/../../ns-3.34/logs/{configname}_{pawmode}/{logstr}/0/1_{mi}_2_4/{parstring}/{smoothcollection}_{smoothwindow}_{smooththreshold}/{ns3seed}/"
                if not os.path.exists(fdir+"tor.tr"):
                    print(f"***Warning: {fdir}tor.tr not found")
                else:
                    print(f"{fdir}tor.tr")
                    statslist = load_stats(fdir,q=1,numqueues=2,numports=numsinks,p=0)
                    if len(statslist[0])<sim*1000: print(f"***Short logs: {configname}, totalbuffer={totalbuffer}, srcbwstr={srcbwstr}, {len(statslist[0])}")
                    datadict[configname]["pie"] = statslist

                # Static
                pawmode="fixed"
                qdisctype="Fifo"
                bufferalg="111"
                startbuffer = BDP
                confstr = f"--targetBW={targetbw}"
                logstr = f"{totalbuffer}_{bufferalg}_0_204_1_{startbuffer}/{srcbwstr}_{targetbw}"
                fdir = f"{mydir}/../../ns-3.34/logs/{configname}_{pawmode}/{logstr}/0/1_{mi}_2_4/{parstring}/{smoothcollection}_{smoothwindow}_{smooththreshold}/{ns3seed}/"
                if not os.path.exists(fdir+"tor.tr"):
                    print(f"***Warning: {fdir}tor.tr not found")
                else:
                    print(f"{fdir}tor.tr")
                    statslist = load_stats(fdir,q=1,numqueues=2,numports=numsinks,p=0)
                    if len(statslist[0])<sim*1000: print(f"***Short logs: {configname}, totalbuffer={totalbuffer}, srcbwstr={srcbwstr}, {len(statslist[0])}")
                    datadict[configname]["static"] = statslist


schemearr = ["titrate","codel","pie","static"]
arrstart = 0
arrend = 200000

thptlist_wrap = dict()
qlenlist_wrap = dict()
for rtt in rttarr:
    for ccaname in ccanamearr:
        for numflowname in numflownamearr:
            thptlist = dict()
            qlenlist = dict()
            for scheme in schemearr:
                thptlist[scheme]=list()
                qlenlist[scheme]=list()
            for confseed in confseedarr:
                configname = f"cca{ccaname}_nf{numflowname}_rtt{rtt}_cseed{confseed}"
                
                for scheme in schemearr:
                    datator = datadict[configname][scheme]
                    time = datator[0][arrstart:arrend]
                    qlen = datator[1][arrstart:arrend]
                    sent = datator[2][arrstart:arrend]
                    thpt = datator[3][arrstart:arrend]
                    drop = datator[4][arrstart:arrend]
                    thres = datator[5][arrstart:arrend]

                    thptlist[scheme].append(sum(thpt)/len(thpt))
                    qlenlist[scheme].append(sum(qlen)/len(qlen) / 1000000 /(1000/8/1000))
            key = f"{rtt}_{ccaname}_{numflowname}"
            thptlist_wrap[key]=thptlist
            qlenlist_wrap[key]=qlenlist


def convert_list_to_str(mylist):
    mystr = ""
    for x in mylist:
        mystr+=f"{x} "
    return mystr

with open(f'{mydir}/data_throughput.txt', 'w') as fthpt:
    with open(f'{mydir}/data_latency.txt', 'w') as flat:
        for rtt in rttarr:
            for ccaname in ccanamearr:
                for numflowname in numflownamearr:
                    key = f"{rtt}_{ccaname}_{numflowname}"
                    thptlist = thptlist_wrap[key]
                    qlenlist = qlenlist_wrap[key]
                    for scheme in schemearr:
                        fthpt.write(f"{rtt} {ccaname} {numflowname} {scheme} {convert_list_to_str(thptlist[scheme])}\n")
                        flat.write(f"{rtt} {ccaname} {numflowname} {scheme} {convert_list_to_str(qlenlist[scheme])}\n")