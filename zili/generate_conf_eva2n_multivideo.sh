fname="eva2n-multivideo.conf"
rm ${fname}
tracedir=traces
for cca in 46
do
    appAType="Video"
    for appAConf in 2 3 4 5
    do
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
