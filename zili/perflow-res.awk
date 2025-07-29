{
    if ($1 < 10000000) {
        rtt_th = 200
        measure_intvl = $1 - last_ts
        last_ts = $1
    }
    else {
        bw[$3] += $5
        rtt[$3] += $7
        cnt[$3] ++
        maxrtt = maxrtt > $7 ? maxrtt : $7
        rttcnt += $7 > rtt_th
    }
}

END {
    for (id in cnt) {
        print id, bw[id] / cnt[id], rtt[id] / cnt[id], maxrtt, rttcnt * measure_intvl
    }
}