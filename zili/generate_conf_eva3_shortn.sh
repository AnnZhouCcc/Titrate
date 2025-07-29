fname="eva3-shortn.conf"
rm ${fname}
tracedir=traces
for cca in 46
do
    appAType="Video"
    for appAConf in 1
    do
        appBType="Web"
        for appBConf in 15005 15010 15020 15040 15060 15080 15100
        do 
            qsize=1200
            for traceid in `seq 0 1 9`
            do
                trace=maxwell_20.0_0.01_300_${traceid}.pitree-trace
                conf="qdisc-test --simDuration=40 --logging --ccaType=${cca} --trace=traces/${trace} --qdiscSize=${qsize} --appAType=${appAType} --appAConf=${appAConf} --appBType=${appBType} --appBConf=${appBConf}"
                logprefix="logs/${cca}_${appAType}${appAConf}_${appBType}${appBConf}/"
                for diffServ in 0
                do
                    echo '"'${conf}' --queueDiscType=PfifoFast --diffServ='${diffServ}'",'${logprefix}'PfifoFast_'${diffServ}'_'${qsize}'/'${trace}'/output.tr' >> ${fname}
                done
                # codelTarget=5ms
                # echo '"'${conf}' --queueDiscType=CoDel --codelTarget='${codelTarget}' --diffServ=0",'${logprefix}'CoDel_0_'${codelTarget}${qsize}'/'${trace}'/output.tr' >> ${fname}
                # echo '"'${conf}' --queueDiscType=Hhf --diffServ=0",'${logprefix}'Hhf_0_'${qsize}'/'${trace}'/output.tr' >> ${fname}
                # echo '"'${conf}' --queueDiscType=Choke --diffServ=0",'${logprefix}'Choke_0_'${qsize}'/'${trace}'/output.tr' >> ${fname}
                # echo '"'${conf}' --queueDiscType=Sfb --diffServ=0",'${logprefix}'Sfb_0_'${qsize}'/'${trace}'/output.tr' >> ${fname}
                # for diffServ in 9 90
                # do 
                #     echo '"'${conf}' --queueDiscType=StrictPriority --diffServ='${diffServ}'",'${logprefix}'StrictPriority_'${diffServ}'_'${qsize}'/'${trace}'/output.tr' >> ${fname}
                # done
                for codelTarget in 500ms
                do
                    echo '"'${conf}' --queueDiscType=FqCoDel --codelTarget='${codelTarget}' --diffServ=0",'${logprefix}'FqCoDel_0_'${codelTarget}${qsize}'/'${trace}'/output.tr' >> ${fname}
                done 
                # you must keep dwrrPrioRatio as 4-char strings, e.g., 1.00 instead of 1!
                for dwrrPrioRatio in 1.00
                do
                    for diffServ in 9
                    do
                        echo '"'${conf}' --queueDiscType=Dwrr --dwrrPrioRatio='${dwrrPrioRatio}' --diffServ='${diffServ}'",'${logprefix}'Dwrr_'${diffServ}'_'${dwrrPrioRatio}'/'${trace}'/output.tr' >> ${fname}
                    done
                done
                for decayRatio in 4.00
                do
                    dwrrPrioRatio=1.00
                    echo '"'${conf}' --queueDiscType=Auto --dwrrPrioRatio='${dwrrPrioRatio}' --diffServ=0 --autoDecayingFunc=ExpClass --autoDecayingCoef='${decayRatio}'",'${logprefix}'Auto_0_'${dwrrPrioRatio}'_ExpClass'${decayRatio}'/'${trace}'/output.tr' >> ${fname}
                done
                # diffServ=9
                # echo '"'${conf}' --queueDiscType=DualQ --diffServ='${diffServ}'",'${logprefix}'DualQ_'${diffServ}'_'${qsize}'/'${trace}'/output.tr' >> ${fname}
            done
        done
    done
done
