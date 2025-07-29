fname="eva6-classify.conf"
rm ${fname}
tracedir=traces
qsize=500
trace=maxwell_20.0_0.01_300_0.pitree-trace
cca=4560
conf="qdisc-test --simDuration=200 --logging --ccaType=${cca} --trace=traces/${trace} --qdiscSize=${qsize} --appAType=Video --appAConf=1 --appBType=Video --appBConf=1 --appCType=Video --appCConf=1 --appDType=Video --appDConf=1"
logprefix="logs/${cca}_Video1_Video1_Video1_Video1/"

echo '"'${conf}' --queueDiscType=PfifoFast --diffServ=0",'${logprefix}'PfifoFast_0_'${qsize}'/'${trace}'/output.tr' >> ${fname}

echo '"'${conf}' --queueDiscType=FqCoDel --codelTarget=500ms --diffServ=0",'${logprefix}'FqCoDel_0_500ms'${qsize}'/'${trace}'/output.tr' >> ${fname}

echo '"'${conf}' --queueDiscType=Hhf --diffServ=0",'${logprefix}'Hhf_0_'${qsize}'/'${trace}'/output.tr' >> ${fname}
                
decayRatio=4.00
dwrrPrioRatio=1.00
echo '"'${conf}' --queueDiscType=Auto --dwrrPrioRatio='${dwrrPrioRatio}' --diffServ=0 --autoDecayingFunc=ExpClass --autoDecayingCoef='${decayRatio}'",'${logprefix}'Auto_0_'${dwrrPrioRatio}'_ExpClass'${decayRatio}'/'${trace}'/output.tr' >> ${fname}
