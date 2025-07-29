{
    bw += $5
    rtt += $7
    cnt++
    if (cnt*50 > 10*(rtt/cnt)) {
        if (bw/cnt > 500000) {
            print $1, bw/cnt, rtt/cnt
        }
        cnt = 0
        bw = 0
        rtt = 0
    }
}