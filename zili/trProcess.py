
import numpy as np
from scipy.special import ndtri

def bwTraceProcessor(trace, startTime, endTime):
    isRto, maxRtt, rttCnt, avgBw = {}, {}, {}, {}
    notFirst, interval = [], {}
    bwSum, bwCnt, bwSeries = {}, {}, {}
    with open(trace, 'r') as f:
        lines = f.readlines()
        for line in lines:
            timestamp = int(line.split()[0])
            flowid = int(line.split()[2])
            bandwidth = int(line.split()[4])
            rtt = int(line.split()[6])
            if flowid not in notFirst:
                lastline = line
                isRto[flowid], maxRtt[flowid], rttCnt[flowid], bwSum[flowid], bwCnt[flowid], bwSeries[flowid] = 0, rtt, 0, 0, 0, []
                interval[flowid] = 0
                notFirst.append(flowid)
                continue

            interval[flowid] += (timestamp - int(lastline.split()[0]) - interval[flowid]) >> 3
            maxRtt[flowid] = rtt if maxRtt[flowid] < rtt else maxRtt[flowid]
            rttCnt[flowid] = rttCnt[flowid] + 1 if rtt > 200 else rttCnt[flowid]
            if timestamp >= startTime and timestamp <= endTime:
                bwSum[flowid] += bandwidth
                bwCnt[flowid] += 1
                bwSeries[flowid].append ([timestamp, bandwidth])
            if len(line.split()) > 7 and (int(line.split()[12]) == 4):
                isRto[flowid] += 1
            
    rttDur = {}
    for flowid in notFirst:
        rttDur[flowid] = rttCnt[flowid] * interval[flowid]
        try:
            avgBw[flowid] = bwSum[flowid] / bwCnt[flowid]
        except ZeroDivisionError:
            avgBw[flowid] = 0
    return isRto, maxRtt, rttDur, avgBw, bwSeries


def fctTraceProcessor(trace):
    lastStart = 0
    firstEnd = 1e10
    with open(trace, 'r') as f:
        start, end = {}, {}
        lines = f.readlines()
        for line in lines:
            timestamp = int(line.split()[0])
            flowid = int(line.split()[2])
            if flowid not in start.keys():
                start[flowid] = timestamp
            end[flowid] = timestamp
    
    fcts = []
    for flowid in start.keys():
        fcts.append(end[flowid] - start[flowid])
        lastStart = max(lastStart, start[flowid])
        firstEnd = min(firstEnd, end[flowid])
    return np.average(fcts), np.max(fcts), lastStart, firstEnd, fcts


def appTraceProcessor(trace, startTime = 14000, duration = 5000):
    with open(trace, 'r') as f:
        lines = f.readlines()
        send = {}
        decode = {}
        for line in lines:
            timestamp = int(line.split()[0])
            flowid = int(line.split()[2])
            action = line.split()[3]
            frameid = int(line.split()[5])
            if flowid not in send.keys():
                send[flowid] = {}
                decode[flowid] = {}

            if (action == "Send"):
                send[flowid][frameid] = timestamp
            elif action in ["Decode", "Discard"]:
                decode[flowid][frameid] = timestamp

    maxDelay, avgDelay, delayDur, allDelay = {}, {}, {}, {}
    for flowid in send.keys():
        delays = []
        for frameid in decode[flowid].keys():
            if decode[flowid][frameid] > startTime and decode[flowid][frameid] < startTime + duration:
                delays.append(decode[flowid][frameid] - send[flowid][frameid])
        maxDelay[flowid] = np.max (delays)
        delayDur[flowid] = np.sum (np.array(delays) > 190) * 20
        avgDelay[flowid] = np.average (delays)
        allDelay[flowid] = delays
    return maxDelay, delayDur, avgDelay, allDelay


def ccaDecode(cca):
    ccaMap = ["Cubic", "Bbr", "Copa", "LinuxReno"]
    return [ccaMap[cca % 10], ccaMap[cca // 10]]


def RemoveOutliers (datas):
    outlierIndex = np.ones (len (datas[0]), dtype=bool)
    for data in datas:
        dataMean = np.mean (data)
        dataStd = np.std (data)
        outlierIndex = outlierIndex & \
            (np.array (data) >= dataMean - ndtri (0.995) * dataStd) & \
            (np.array (data) <= dataMean + ndtri (0.995) * dataStd)
    newdatas = []
    for data in datas:
        data = [data[i] for i in range(len (data)) if outlierIndex[i]]
        newdatas.append (data)
    return newdatas