/Send/ {
    send[$6] = $1
}

/Decode || Discard/ {
    decode[$6] = $1
}

END {
    for (id in decode) {
        print send[id], decode[id] - send[id]
    }
}