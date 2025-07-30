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
sim=2
mi=500
parstring = "5_10_50_3_3_3_5_10_3_5_1_10_5"
smooththreshold = 0

ccanamearr=["realmix"]
numflownamearr=["large"]
rttarr=[300]
mrnq=12
confseedarr = [0]

myrtt=rttarr[0]
mymidbw=1000
BDP = int(myrtt/1000 * mymidbw * 1000000/8)
totalbuffer = 2*BDP
startbuffer = totalbuffer
targetbw = int(totalbuffer//10)




# ###################
# # Processing data #
# ###################

datadict = dict()
for confseed in confseedarr:
    for cca in ccanamearr:
        for numflow in numflownamearr:
            for rtt in rttarr:
                configname = f"cca{cca}_nf{numflow}_rtt{rtt}_cseed{confseed}"
                conffile = f"../detailed_ae/deltabw/configurations/{configname}.conf"
                dbwname = "conf0"
                dbwfile = f"../detailed_ae/deltabw/dbwconfigurations/{dbwname}.conf"
                
                datadict[f"{configname}_{dbwname}"] = dict()
                datadict[f"{configname}_{dbwname}"]["titrate"] = dict()
                datadict[f"{configname}_{dbwname}"]["codel"] = dict()
                datadict[f"{configname}_{dbwname}"]["pie"] = dict()
                datadict[f"{configname}_{dbwname}"]["static"] = dict()

                startbuffer = totalbuffer
                targetbw = int(totalbuffer//10)  

                # Titrate
                pawmode="paw"
                qdisctype="Fifo"
                bufferalg="111"
                confstr = f"--targetBW={targetbw} --mainRoomNumQueues={mrnq} --deltaBwConfigFile={dbwfile}"
                logstr = f"_{dbwname}/{totalbuffer}_{bufferalg}_0_204_{mrnq}_{startbuffer}/{srcbwstr}_{targetbw}"
                fdir = f"{mydir}/../../ns-3.34/logs/{configname}_{pawmode}{logstr}/0/1_{mi}_2_4/{parstring}/{smoothcollection}_{smoothwindow}_{smooththreshold}/{ns3seed}/"
                if not os.path.exists(fdir+"tor.tr"):
                    print(f"***Warning: {fdir}tor.tr not found")
                else:
                    print(f"{fdir}tor.tr")
                    statslist = load_stats(fdir,q=1,numqueues=2,numports=numsinks,p=0)
                    if len(statslist[0])<sim*1000: print(f"***Short logs: {configname}, totalbuffer={totalbuffer}, srcbwstr={srcbwstr}, {len(statslist[0])}")
                    datadict[f"{configname}_{dbwname}"]["titrate"] = statslist

                # CoDel
                pawmode="fixed"
                qdisctype="CoDel"
                bufferalg="101"
                alpha=8
                confstr = f"--codelTarget=5ms --codelInterval=100ms --alphaString={alpha} --targetBW={targetbw} --deltaBwConfigFile={dbwfile}"
                logstr = f"_{dbwname}/codel_5ms_100ms/{totalbuffer}_{bufferalg}_{alpha}_204_1_{startbuffer}/{srcbwstr}_{targetbw}"
                fdir = f"{mydir}/../../ns-3.34/logs/{configname}_{pawmode}{logstr}/0/1_{mi}_2_4/{parstring}/{smoothcollection}_{smoothwindow}_{smooththreshold}/{ns3seed}/"
                if not os.path.exists(fdir+"tor.tr"):
                    print(f"***Warning: {fdir}tor.tr not found")
                else:
                    print(f"{fdir}tor.tr")
                    statslist = load_stats(fdir,q=1,numqueues=2,numports=numsinks,p=0)
                    if len(statslist[0])<sim*1000: print(f"***Short logs: {configname}, totalbuffer={totalbuffer}, srcbwstr={srcbwstr}, {len(statslist[0])}")
                    datadict[f"{configname}_{dbwname}"]["codel"] = statslist

                # PIE
                pawmode="fixed"
                qdisctype="Pie"
                bufferalg="101"
                alpha=8
                confstr = f"--pieTarget=20ms --alphaString={alpha} --targetBW={targetbw} --deltaBwConfigFile={dbwfile}"
                logstr = f"_{dbwname}/pie_20ms/{totalbuffer}_{bufferalg}_{alpha}_204_1_{startbuffer}/{srcbwstr}_{targetbw}"
                fdir = f"{mydir}/../../ns-3.34/logs/{configname}_{pawmode}{logstr}/0/1_{mi}_2_4/{parstring}/{smoothcollection}_{smoothwindow}_{smooththreshold}/{ns3seed}/"
                if not os.path.exists(fdir+"tor.tr"):
                    print(f"***Warning: {fdir}tor.tr not found")
                else:
                    print(f"{fdir}tor.tr")
                    statslist = load_stats(fdir,q=1,numqueues=2,numports=numsinks,p=0)
                    if len(statslist[0])<sim*1000: print(f"***Short logs: {configname}, totalbuffer={totalbuffer}, srcbwstr={srcbwstr}, {len(statslist[0])}")
                    datadict[f"{configname}_{dbwname}"]["pie"] = statslist

                # Static
                pawmode="fixed"
                qdisctype="Fifo"
                bufferalg="111"
                startbuffer = BDP
                confstr = f"--targetBW={targetbw} --deltaBwConfigFile={dbwfile}"
                logstr = f"_{dbwname}/{totalbuffer}_{bufferalg}_0_204_1_{startbuffer}/{srcbwstr}_{targetbw}"
                fdir = f"{mydir}/../../ns-3.34/logs/{configname}_{pawmode}{logstr}/0/1_{mi}_2_4/{parstring}/{smoothcollection}_{smoothwindow}_{smooththreshold}/{ns3seed}/"
                if not os.path.exists(fdir+"tor.tr"):
                    print(f"***Warning: {fdir}tor.tr not found")
                else:
                    print(f"{fdir}tor.tr")
                    statslist = load_stats(fdir,q=1,numqueues=2,numports=numsinks,p=0)
                    if len(statslist[0])<sim*1000: print(f"***Short logs: {configname}, totalbuffer={totalbuffer}, srcbwstr={srcbwstr}, {len(statslist[0])}")
                    datadict[f"{configname}_{dbwname}"]["static"] = statslist


schemearr = ["titrate","codel","pie","static"]
arrstart = 0
arrend = 200000 
bwarr=[1000,500,750,250]

timeserieslist = dict()
for scheme in schemearr:
    datator = datadict[f"{configname}_{dbwname}"][scheme]
    time = datator[0][arrstart:arrend]
    qlen = datator[1][arrstart:arrend]
    sent = datator[2][arrstart:arrend]
    thpt = datator[3][arrstart:arrend]
    drop = datator[4][arrstart:arrend]
    thres = datator[5][arrstart:arrend]

    if scheme=="titrate":
        timeserieslist["titrate_time"] = [x/1000000000 for x in time[1000:]]
        timeserieslist["titrate_qlen"] = [x/1000000 for x in qlen[1000:]]
        timeserieslist["titrate_thres"] = [x/1000000 for x in thres[1000:]]
    else:
        timeserieslist[f"{scheme}_time"] = [x/1000000000 for x in time[1000:]]
        timeserieslist[f"{scheme}_qlen"] = [x/1000000 for x in qlen[1000:]]

thptlist = dict()
for scheme in schemearr:
    datator = datadict[f"{configname}_{dbwname}"][scheme]
    time = datator[0][arrstart:arrend]
    qlen = datator[1][arrstart:arrend]
    sent = datator[2][arrstart:arrend]
    thpt = datator[3][arrstart:arrend]
    drop = datator[4][arrstart:arrend]
    thres = datator[5][arrstart:arrend]

    thptlist[scheme] = list()
    for start in [0,50000,100000,150000]:
        end = start+50000
        if start==0: start=10000
        if end<=len(thpt): thptlist[scheme].append(sum(thpt[start:end])/(end-start))

qlatlist = dict()
for isch,scheme in enumerate(schemearr):
    datator = datadict[f"{configname}_{dbwname}"][scheme]
    time = datator[0][arrstart:arrend]
    qlen = datator[1][arrstart:arrend]
    sent = datator[2][arrstart:arrend]
    thpt = datator[3][arrstart:arrend]
    drop = datator[4][arrstart:arrend]
    thres = datator[5][arrstart:arrend]

    qlatlist[scheme] = list()
    for istart,start in enumerate([0,50000,100000,150000]):
        end = start+50000
        if start==0: start=10000
        if end<=len(qlen): 
            # print(f"{scheme},{end},{len(qlen)}")
            qlatlist[scheme].append((sum(qlen[start:end])/(end-start))/(bwarr[isch]/8)/1000)


def convert_list_to_str(mylist):
    mystr = ""
    for x in mylist:
        mystr+=f"{x} "
    return mystr

with open('data_timeseries.txt', 'w') as f:
    for scheme in schemearr:
        for tstype in ["time","qlen","thres"]:
            if scheme!="titrate" and tstype!="qlen": continue
            mylist = timeserieslist[f"{scheme}_{tstype}"]
            f.write(f"{scheme} {tstype} {convert_list_to_str(mylist)}\n")
with open('data_throughput.txt', 'w') as f:
    for scheme in schemearr:
        f.write(f"{scheme} {convert_list_to_str(thptlist[scheme])}\n")
with open('data_latency.txt', 'w') as f:
    for scheme in schemearr:
        f.write(f"{scheme} {convert_list_to_str(qlatlist[scheme])}\n")