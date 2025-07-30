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
import xmltodict

def load_webtrace_xml(xmlfile,nfconfigfile,flowstart=201,flowend=1298):
    # nfconfigfile = "/u/az6922/Buffer/experiments/nsdi26sp/design_burst/nfconfigurations/webtraces10_ccacubic_nf100_rtt10_cseed0.conf"
    webtracesdict = dict()
    with open(nfconfigfile,'r') as f:
        lines = f.readlines()
        for line in lines:
            tokens = line.split()
            webtracesdict[int(tokens[0])] = int(tokens[2])

    # xmlfile = '/u/az6922/Buffer/ns-3.34/logs/webtraces10_ccacubic_nf100_rtt10_cseed0_paw/5000000_111_0_204_12_2500000/200_0/0/1_500_2_4/5_10_50_20_3_3_5_10_3_5_5_10_5/500_100_500000/1/flowmonitor.xml'
    with open(xmlfile) as f:
        data = xmltodict.parse(f.read())

    flowidlist = range(flowstart,flowend,2)
    flows = data['FlowMonitor']['FlowStats']['Flow']
    whichwt = 0
    countwt = 0
    starttimelist = list()
    endtimelist = list()
    numdroplist = list()
    resultlist = [[],[],[]]
    for flowid in flowidlist:
        # print(flowid)
        flow = data['FlowMonitor']['FlowStats']['Flow'][flowid]
        starttime = int(float(flow['@timeFirstTxPacket'][:-2]))
        endtime = int(float(flow['@timeLastRxPacket'][:-2]))
        numdrop = int(flow['@lostPackets'])
        flowback = data['FlowMonitor']['FlowStats']['Flow'][flowid+1]
        numdropback = int(flowback['@lostPackets'])
        if countwt < webtracesdict[whichwt]:
            starttimelist.append(starttime)
            endtimelist.append(endtime)
            numdroplist.append(numdrop+numdropback)
            countwt+=1
        else:
            resultlist[0].append(starttimelist)
            resultlist[1].append(endtimelist)
            resultlist[2].append(numdroplist)
            starttimelist = list()
            endtimelist = list()
            numdroplist = list()
            whichwt+=1
            countwt=0
            starttimelist.append(starttime)
            endtimelist.append(endtime)
            numdroplist.append(numdrop+numdropback)
            countwt+=1
    resultlist[0].append(starttimelist)
    resultlist[1].append(endtimelist)
    resultlist[2].append(numdroplist)

    return resultlist




######################
# Setting parameters #
######################

from pathlib import Path
script_path = Path(__file__).resolve()
mydir = script_path.parent


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
totalbufferarr = [24000000,20000000,16000000,12000000]

webtraceidlist = [967, 652, 959, 35, 521, 707, 702, 547, 795, 461]
numflowinburst = 1
appstartarr = range(20,120,10)
srclinkrate = 2




# ###################
# # Processing data #
# ###################

datadict = dict()
flowstart=100
flowend=3196
for totalbuffer in totalbufferarr:
    configname = f"multiburst0"
    conffile = f"{mydir}/configurations/{configname}.conf"
    nfconffile = f"{mydir}/nfconfigurations/{configname}.conf"
    
    startbuffer = int(totalbuffer//10)
    targetbw = int(totalbuffer//10)

    datadict[f"{configname}_{totalbuffer}"] = dict()
    datadict[f"{configname}_{totalbuffer}"]["titrate"] = dict()
    datadict[f"{configname}_{totalbuffer}"]["titrate"]["tor"] = dict()
    datadict[f"{configname}_{totalbuffer}"]["dt1"] = dict()
    datadict[f"{configname}_{totalbuffer}"]["dt1"]["tor"] = dict()

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
            datadict[f"{configname}_{totalbuffer}"]["titrate"]["tor"][port] = statslist
    if os.path.exists(f"{fdir}flowmonitor.xml"):
        xmllist = load_webtrace_xml(f"{fdir}flowmonitor.xml",nfconffile,flowstart,flowend)
        datadict[f"{configname}_{totalbuffer}"]["titrate"]["xml"] = xmllist
    
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
            datadict[f"{configname}_{totalbuffer}"]["dt1"]["tor"][port] = statslist
    if os.path.exists(f"{fdir}flowmonitor.xml"):
        xmllist = load_webtrace_xml(f"{fdir}flowmonitor.xml",nfconffile,flowstart,flowend)
        datadict[f"{configname}_{totalbuffer}"]["dt1"]["xml"] = xmllist


schemearr = ["titrate","dt1"]
numwebtraces = len(webtraceidlist)
arrstart = 0
arrend = 200000 

thptlist = dict()
qlenlist = dict()
wwwtdroplist = dict()
wwwtdurationlist = dict()
for scheme in schemearr:
    for totalbuffer in totalbufferarr:
        configname = f"multiburst0_{totalbuffer}"
        key = f"{totalbuffer}_{scheme}"

        port = 1
        datator = datadict[configname][scheme]["tor"][port]
        time = datator[0][arrstart:arrend]
        qlen = datator[1][arrstart:arrend]
        sent = datator[2][arrstart:arrend]
        thpt = datator[3][arrstart:arrend]
        drop = datator[4][arrstart:arrend]
        thres = datator[5][arrstart:arrend]
        thptlist[key]=(sum(thpt)/len(thpt))
        qlenlist[key]=(sum(qlen)/len(qlen)/1000000)

        dataxml = datadict[configname][scheme]["xml"]
        wtstartlist = dataxml[0][arrstart:arrend]
        wtendlist = dataxml[1][arrstart:arrend]
        wtdrop = dataxml[2][arrstart:arrend]

        wtduration = list()
        wtsumdrop = list()
        for i in range(numwebtraces):
            wtduration.append(max(wtendlist[i])-min(wtstartlist[i]))
            wtsumdrop.append(sum(wtdrop[i]))

        wwwtdurationlist[key]=(sum(wtduration)/len(wtduration)/1000000)
        wwwtdroplist[key]=(sum(wtsumdrop)/len(wtsumdrop))


def convert_list_to_str(mylist):
    mystr = ""
    for x in mylist:
        mystr+=f"{x} "
    return mystr

with open(f'{homedir}Titrate/detailed_ae/multiburst/data_throughput.txt', 'w') as f:
    for scheme in schemearr:
        for totalbuffer in totalbufferarr:
            key = f"{totalbuffer}_{scheme}"
            f.write(f"{totalbuffer} {scheme} {(thptlist[key])}\n")
with open(f'{homedir}Titrate/detailed_ae/multiburst/data_latency.txt', 'w') as f:
    for scheme in schemearr:
        for totalbuffer in totalbufferarr:
            key = f"{totalbuffer}_{scheme}"
            f.write(f"{totalbuffer} {scheme} {(qlenlist[key])}\n")
with open(f'{homedir}Titrate/detailed_ae/multiburst/data_ndrop.txt', 'w') as f:
    for scheme in schemearr:
        for totalbuffer in totalbufferarr:
            key = f"{totalbuffer}_{scheme}"
            f.write(f"{totalbuffer} {scheme} {(wwwtdroplist[key])}\n")
with open(f'{homedir}Titrate/detailed_ae/multiburst/data_bct.txt', 'w') as f:
    for scheme in schemearr:
        for totalbuffer in totalbufferarr:
            key = f"{totalbuffer}_{scheme}"
            f.write(f"{totalbuffer} {scheme} {(wwwtdurationlist[key])}\n")