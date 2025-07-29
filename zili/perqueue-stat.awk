{
    for (i = 1; i < NF/4; i++) {
        qlen[$(4*i)] += $(4*i+1)
    }
}

END {
    for (var in qlen) {
        print var, qlen[var] / NR
    }
}