/NodeList\/0\/DeviceList\/1\/\$ns3::PointToPointNetDevice\/TxQueue\/Enqueue/ {
    send[$35][$19] = $2
}

/NodeList\/2\/DeviceList\/1\/\$ns3::PointToPointNetDevice\/MacRx/ {
    recv[$35][$19] = $2
    print $2, $19, int ((recv[$35][$19] - send[$35][$19]) * 1000) >> "pkt-delay"$35".tr"
}