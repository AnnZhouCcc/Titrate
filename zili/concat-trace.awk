BEGIN {
    avgInterval = 0
    lastTimestamp = 0
    curTimeshift = 0
}

{
    if ($1 + curTimeshift < lastTimestamp) {
        curTimeshift = lastTimestamp - $1 + avgInterval
    }
    avgInterval += 0.125 * ($1 + curTimeshift - lastTimestamp - avgInterval)
    lastTimestamp = $1 + curTimeshift
    print lastTimestamp, $2
}