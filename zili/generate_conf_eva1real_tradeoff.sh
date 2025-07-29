fname="eva1real-tradeoff.conf"
rm ${fname}
tracedir=traces
for cca in 49 46 45 4fff0
do
    appAType="Video"
    appAConf=1
    appBType="Web"
    for webtrace in `ls ns-3.34/webtraces`
    do 
        appBConf=${webtrace%".log"}"000"
        qsize=1000
        traceid=0
        trace=maxwell_20.0_0.01_300_${traceid}.pitree-trace
        if [[ "${cca}" == "4fff0" ]]
        then
            webapp=E
            simDuration=80
        else
            webapp=B
            simDuration=40
        fi
        conf="qdisc-test --simDuration=${simDuration} --logging --ccaType=${cca} --trace=traces/${trace} --qdiscSize=${qsize} --appAType=${appAType} --appAConf=${appAConf} --app${webapp}Type=${appBType} --app${webapp}Conf=${appBConf}"
        if [[ "${cca}" == "45" ]]
        then
            conf="${conf} --sockBufDutyRatio=0.75"
        fi
        
        logprefix="logs/${cca}_${appAType}${appAConf}_${appBType}${appBConf}/"
        
        # FIFO
        echo '"'${conf}' --queueDiscType=PfifoFast --diffServ=0",'${logprefix}'PfifoFast_0_'${qsize}'/'${trace}'/output.tr' >> ${fname}
        
        # HHF
        echo '"'${conf}' --queueDiscType=Hhf --diffServ=0",'${logprefix}'Hhf_0_'${qsize}'/'${trace}'/output.tr' >> ${fname}
        
        # SJF and StrictPriority
        for diffServ in 9 90
        do 
            echo '"'${conf}' --queueDiscType=StrictPriority --diffServ='${diffServ}'",'${logprefix}'StrictPriority_'${diffServ}'_'${qsize}'/'${trace}'/output.tr' >> ${fname}
        done

        # FQ
        codelTarget=500ms
        echo '"'${conf}' --queueDiscType=FqCoDel --codelTarget='${codelTarget}' --diffServ=0",'${logprefix}'FqCoDel_0_'${codelTarget}${qsize}'/'${trace}'/output.tr' >> ${fname}
            
        # CBQ (1:1), (1:5)
        # you must keep dwrrPrioRatio as 4-char strings, e.g., 1.00 instead of 1!
        for dwrrPrioRatio in 1.00 0.20
        do
            diffServ=9
            echo '"'${conf}' --queueDiscType=Dwrr --dwrrPrioRatio='${dwrrPrioRatio}' --diffServ='${diffServ}'",'${logprefix}'Dwrr_'${diffServ}'_'${dwrrPrioRatio}'/'${trace}'/output.tr' >> ${fname}
        done

        # Confucius
        decayRatio=4.00
        dwrrPrioRatio=1.00
        echo '"'${conf}' --queueDiscType=Auto --dwrrPrioRatio='${dwrrPrioRatio}' --diffServ=0 --autoDecayingFunc=ExpClass --autoDecayingCoef='${decayRatio}'",'${logprefix}'Auto_0_'${dwrrPrioRatio}'_ExpClass'${decayRatio}'/'${trace}'/output.tr' >> ${fname}
        
        # CoDel
        codelTarget=5ms
        echo '"'${conf}' --queueDiscType=CoDel  --codelTarget='${codelTarget}' --diffServ=0",'${logprefix}'CoDel_0_'${codelTarget}${qsize}'/'${trace}'/output.tr' >> ${fname}
        
        # RED
        echo '"'${conf}' --queueDiscType=Red --diffServ=0",'${logprefix}'Red_0_'${qsize}'/'${trace}'/output.tr' >> ${fname}
        
        # DualQ
        diffServ=9
        echo '"'${conf}' --queueDiscType=DualQ --diffServ='${diffServ}'",'${logprefix}'DualQ_'${diffServ}'_'${qsize}'/'${trace}'/output.tr' >> ${fname}
    done
done
