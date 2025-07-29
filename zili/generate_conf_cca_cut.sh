fname="cca-cut.conf"
rm ${fname}
tracedir=traces
for cca in 0 5 6 9
do
    appAType="Video"
    appAConf=1

    for qsize in 250
    do
        for trace in `ls ns-3.34/traces | grep cut`
        do
            logdir="logs-design-cca/"
            conf="qdisc-test --logdir=${logdir} --sockBufDutyRatio=1 --logging --ccaType=${cca} --trace=traces/${trace} --qdiscSize=${qsize} --appAType=${appAType} --appAConf=${appAConf}"
            logprefix="${logdir}${cca}_${appAType}${appAConf}/"
            resetQueueDisc=`echo ${trace} | awk -F '[-.]' '{printf("%d%02d\n", $3, $2)}'`
            echo '"'${conf}' --queueDiscType=PfifoFast --diffServ=0 --resetQueueDisc='${resetQueueDisc}'",'${logprefix}'PfifoFast_0_'${qsize}'/'${trace}'/output.tr' >> ${fname}
        done
    done
done
