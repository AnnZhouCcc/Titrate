awk -F '[ Mm]' '{
    if ($1 > 0.06) {
        print $0
    }
}' $2/$1 > $1

# bw < 60kbps -- schedule event in p2pchannel will longer than 200ms
#     in this case, even bw recovers later, channel still not works
# rtt and loss do not exist in zhuge traces -- only in hairpin traces.