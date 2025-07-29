BEGIN {
    isRto = 0
}

{
    if ($11 == 4)
        isRto = 1
}

END {
    print isRto
}