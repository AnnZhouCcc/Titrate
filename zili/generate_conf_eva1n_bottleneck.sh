fname="evan1-bottleneck.conf"
rm ${fname}
tracedir=traces
for cca in 46
do
    appAType="Video"
    appAConf=1
    appBType="Web"
    for webtrace in `ls ns-3.34/webtraces`
    do 
        appBConf=${webtrace%".log"}"000"
        qsize=1000
        webapp=B
        simDuration=40
        for btlbw in 1 2 3
        do
            srcBw=1000Mbps
            midBw=1000Mbps
            dstBw=1000Mbps
            if [[ "${btlbw}" == "1" ]]
            then
                srcBw=20Mbps
            elif [[ "${btlbw}" == "2" ]]
            then
                midBw=20Mbps
            elif [[ "${btlbw}" == "3" ]]
            then
                dstBw=20Mbps
            fi
            
            conf="qdisc-test --simDuration=${simDuration} --logging --ccaType=${cca} --qdiscSize=${qsize} --appAType=${appAType} --appAConf=${appAConf} --app${webapp}Type=${appBType} --app${webapp}Conf=${appBConf} --hop=3 --srcBandwidth=${srcBw} --midBandwidth=${midBw} --dstBandwidth=${dstBw}"
            logprefix="logs/${cca}_${appAType}${appAConf}_${appBType}${appBConf}_${srcBw}_${midBw}_${dstBw}/"
            
            # FIFO
            echo '"'${conf}' --queueDiscType=PfifoFast --diffServ=0",'${logprefix}'PfifoFast_0_'${qsize}'/'${trace}'/output.tr' >> ${fname}
            
            # FQ
            codelTarget=500ms
            echo '"'${conf}' --queueDiscType=FqCoDel --codelTarget='${codelTarget}' --diffServ=0",'${logprefix}'FqCoDel_0_'${codelTarget}${qsize}'/'${trace}'/output.tr' >> ${fname}
                
            # CBQ (1:1), (1:5)
            # you must keep dwrrPrioRatio as 4-char strings, e.g., 1.00 instead of 1!
            for dwrrPrioRatio in 1.00
            do
                diffServ=9
                echo '"'${conf}' --queueDiscType=Dwrr --dwrrPrioRatio='${dwrrPrioRatio}' --diffServ='${diffServ}'",'${logprefix}'Dwrr_'${diffServ}'_'${dwrrPrioRatio}'/'${trace}'/output.tr' >> ${fname}
            done

            # Confucius
            decayRatio=4.00
            dwrrPrioRatio=1.00
            echo '"'${conf}' --queueDiscType=Auto --dwrrPrioRatio='${dwrrPrioRatio}' --diffServ=0 --autoDecayingFunc=ExpClass --autoDecayingCoef='${decayRatio}'",'${logprefix}'Auto_0_'${dwrrPrioRatio}'_ExpClass'${decayRatio}'/'${trace}'/output.tr' >> ${fname}
        done
    done
done
