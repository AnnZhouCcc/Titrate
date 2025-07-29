{
    if (!start[$3]) {
        start[$3] = $1
    }
    end[$3] = $1
}

END {
    for (idx in start) {
        print idx, end[idx] - start[idx]
    }
}