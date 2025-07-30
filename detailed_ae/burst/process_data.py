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
import xmltodict

def load_webtrace_xml(xmlfile,nfconfigfile,flowstart=201,flowend=1298):
    # print(f"{flowstart},{flowend}")
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
    # print(len(flows))
    whichwt = 0
    countwt = 0
    starttimelist = list()
    endtimelist = list()
    numdroplist = list()
    resultlist = [[],[],[]]
    for flowid in flowidlist:
        # print(f"flowid={flowid}")
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





# ###################
# # Processing data #
# ###################

datadict = dict()
for confseed in confseedarr:
    for ccaname in ccanamearr:
        for numflowname in numflownamearr:
            for rtt in rttarr:
                for i,wtid in enumerate(webtraceidlist):
                    configname = f"wt{wtid}_cca{ccaname}_nf{numflowname}_rtt{rtt}_cseed{confseed}"
                    conffile = f"{mydir}/configurations/wt{wtid}_cca{ccaname}_nf{numflowname}_rtt{rtt}_cseed{confseed}.conf"
                    nfconffile = f"{mydir}/nfconfigurations/wt{wtid}_cca{ccaname}_nf{numflowname}_rtt{rtt}_cseed{confseed}.conf"
                    with open(conffile,'r') as f:
                        line = f.readlines()[0]
                        tokens = line.split()
                        totalnumflow = int(tokens[0])
                    with open(nfconffile,'r') as f:
                        line = f.readlines()[0]
                        tokens = line.split()
                        burstnumflow = int(tokens[2])
                    longnumflow = totalnumflow-burstnumflow
                    flowstart = longnumflow*2
                    flowend = totalnumflow*2

                    datadict[configname] = dict()
                    datadict[configname]["titrate"] = dict()
                    datadict[configname]["p"] = dict()
                    datadict[configname]["codel"] = dict()
                    datadict[configname]["pie"] = dict()
                    datadict[configname]["static"] = dict()

                    BDP = int(rtt/1000 * midbw * 1000000/8)
                    totalbuffer = 2*BDP
                    targetbw = int(totalbuffer//10)
                    smoothwindow=100

                    # Titrate
                    pawmode="paw"
                    qdisctype="Fifo"
                    bufferalg="111"
                    startbuffer = int(totalbuffer//10)
                    confstr = f"--targetBW={targetbw} --mainRoomNumQueues={mrnq}"
                    logstr = f"{totalbuffer}_{bufferalg}_0_204_{mrnq}_{startbuffer}/{srcbwstr}_{targetbw}"
                    fdir = f"{mydir}/../../ns-3.34/logs/{configname}_{pawmode}/{logstr}/0/1_{mi}_2_4/{parstring}/{smoothcollection}_{smoothwindow}_{smooththreshold}/{ns3seed}/"
                    if not os.path.exists(fdir+"tor.tr"):
                        print(f"***Warning: {fdir}tor.tr not found")
                    else:
                        print(f"{fdir}tor.tr")
                        statslist = load_stats(fdir,q=1,numqueues=2,numports=numsinks,p=0)
                        if len(statslist[0])<sim*1000: print(f"***Short logs: {configname}, totalbuffer={totalbuffer}, srcbwstr={srcbwstr}, {len(statslist[0])}")
                        datadict[configname]["titrate"]["tor"] = statslist
                    if os.path.exists(f"{fdir}flowmonitor.xml"):
                        xmllist = load_webtrace_xml(f"{fdir}flowmonitor.xml",nfconffile,flowstart,flowend)
                        datadict[configname]["titrate"]["xml"] = xmllist

                    # p
                    pawmode="p"
                    qdisctype="Fifo"
                    bufferalg="111"
                    startbuffer = int(totalbuffer//10)
                    confstr = f"--targetBW={targetbw} --mainRoomNumQueues={mrnq}"
                    logstr = f"{totalbuffer}_{bufferalg}_0_204_{mrnq}_{startbuffer}/{srcbwstr}_{targetbw}"
                    fdir = f"{mydir}/../../ns-3.34/logs/{configname}_{pawmode}/{logstr}/0/1_{mi}_2_4/{parstring}/{smoothcollection}_{smoothwindow}_{smooththreshold}/{ns3seed}/"
                    if not os.path.exists(fdir+"tor.tr"):
                        print(f"***Warning: {fdir}tor.tr not found")
                    else:
                        print(f"{fdir}tor.tr")
                        statslist = load_stats(fdir,q=1,numqueues=2,numports=numsinks,p=0)
                        if len(statslist[0])<sim*1000: print(f"***Short logs: {configname}, totalbuffer={totalbuffer}, srcbwstr={srcbwstr}, {len(statslist[0])}")
                        datadict[configname]["p"]["tor"] = statslist
                    if os.path.exists(f"{fdir}flowmonitor.xml"):
                        xmllist = load_webtrace_xml(f"{fdir}flowmonitor.xml",nfconffile,flowstart,flowend)
                        datadict[configname]["p"]["xml"] = xmllist
                    
                    startbuffer = totalbuffer
                    smoothwindow = 600

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
                        datadict[configname]["codel"]["tor"] = statslist
                    if os.path.exists(f"{fdir}flowmonitor.xml"):
                        xmllist = load_webtrace_xml(f"{fdir}flowmonitor.xml",nfconffile,flowstart,flowend)
                        datadict[configname]["codel"]["xml"] = xmllist

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
                        datadict[configname]["pie"]["tor"] = statslist
                    if os.path.exists(f"{fdir}flowmonitor.xml"):
                        xmllist = load_webtrace_xml(f"{fdir}flowmonitor.xml",nfconffile,flowstart,flowend)
                        datadict[configname]["pie"]["xml"] = xmllist

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
                        datadict[configname]["static"]["tor"] = statslist
                    if os.path.exists(f"{fdir}flowmonitor.xml"):
                        xmllist = load_webtrace_xml(f"{fdir}flowmonitor.xml",nfconffile,flowstart,flowend)
                        datadict[configname]["static"]["xml"] = xmllist

schemearr = ["titrate","p","codel","pie","static"]
ccaname = ccanamearr[0]
numflowname = numflownamearr[0]
rtt = rttarr[0]
arrstart = 0 
arrend = 20000

bct_sorted_dict = dict()
for ischeme,scheme in enumerate(schemearr):
    for iw,wtid in enumerate(webtraceidlist):

        wwwtdroplist = list()
        wwwtdurationlist = list()
        for confseed in confseedarr:
            configname = f"wt{wtid}_cca{ccaname}_nf{numflowname}_rtt{rtt}_cseed{confseed}"
        
            datator = datadict[configname][scheme]["tor"]
            time = datator[0][arrstart:arrend]
            qlen = datator[1][arrstart:arrend]
            sent = datator[2][arrstart:arrend]
            thpt = datator[3][arrstart:arrend]
            drop = datator[4][arrstart:arrend]
            thres = datator[5][arrstart:arrend]

            avgqlat = (sum(qlen)/len(qlen) / 1000000) /(1000/8)*1000000000

            dataxml = datadict[configname][scheme]["xml"]
            wtstartlist = dataxml[0][arrstart:arrend]
            wtendlist = dataxml[1][arrstart:arrend]
            wtdrop = dataxml[2][arrstart:arrend]
            for i in range(numwebtraces):
                wwwtdurationlist.append((max(wtendlist[i])-min(wtstartlist[i])-avgqlat)/1000000000)
                wwwtdroplist.append(sum(wtdrop[i]))

        sorted_wwwtdurationlist = np.sort(wwwtdurationlist)
        bct_sorted_dict[f"{wtid}_{scheme}"] = sorted_wwwtdurationlist

def convert_list_to_str(mylist):
    mystr = ""
    for x in mylist:
        mystr+=f"{x} "
    return mystr

with open(f'{mydir}/data.txt', 'w') as f:
    for wtid in webtraceidlist:
        for scheme in schemearr:
            key = f"{wtid}_{scheme}"
            f.write(f"{wtid} {scheme} {convert_list_to_str(bct_sorted_dict[key])}\n")