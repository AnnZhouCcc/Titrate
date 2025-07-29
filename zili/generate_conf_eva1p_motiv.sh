fname="eva1p-motiv.conf"
rm ${fname}
tracedir=traces
cca=46
appAType="Video"
appAConf=1
appBType="Web"
webtrace='8.log'
appBConf=${webtrace%".log"}"000"
qsize=1000
traceid=0
trace=maxwell_20.0_0.01_300_${traceid}.pitree-trace
webapp=B
simDuration=40
conf="qdisc-test --simDuration=${simDuration} --logging --ccaType=${cca} --trace=traces/${trace} --qdiscSize=${qsize} --appAType=${appAType} --appAConf=${appAConf} --app${webapp}Type=${appBType} --app${webapp}Conf=${appBConf}"
logprefix="logs/${cca}_${appAType}${appAConf}_${appBType}${appBConf}/"

# FQ
codelTarget=500ms
echo '"'${conf}' --queueDiscType=FqCoDel --codelTarget='${codelTarget}' --diffServ=0",'${logprefix}'FqCoDel_0_'${codelTarget}${qsize}'/'${trace}'/output.tr' >> ${fname}

# FIFO
echo '"'${conf}' --queueDiscType=PfifoFast --diffServ=0",'${logprefix}'PfifoFast_0_'${qsize}'/'${trace}'/output.tr' >> ${fname}

# Confucius
decayRatio=4.00
dwrrPrioRatio=1.00
echo '"'${conf}' --queueDiscType=Auto --dwrrPrioRatio='${dwrrPrioRatio}' --diffServ=0 --autoDecayingFunc=ExpClass --autoDecayingCoef='${decayRatio}'",'${logprefix}'Auto_0_'${dwrrPrioRatio}'_ExpClass'${decayRatio}'/'${trace}'/output.tr' >> ${fname}

# CBQ (1:1)
dwrrPrioRatio=1.00
diffServ=9
echo '"'${conf}' --queueDiscType=Dwrr --dwrrPrioRatio='${dwrrPrioRatio}' --diffServ='${diffServ}'",'${logprefix}'Dwrr_'${diffServ}'_'${dwrrPrioRatio}'/'${trace}'/output.tr' >> ${fname}

# CBQ (1:5)
dwrrPrioRatio=0.20
diffServ=9
echo '"'${conf}' --queueDiscType=Dwrr --dwrrPrioRatio='${dwrrPrioRatio}' --diffServ='${diffServ}'",'${logprefix}'Dwrr_'${diffServ}'_'${dwrrPrioRatio}'/'${trace}'/output.tr' >> ${fname}

# StrictPriority
diffServ=9
echo '"'${conf}' --queueDiscType=StrictPriority --diffServ='${diffServ}'",'${logprefix}'StrictPriority_'${diffServ}'_'${qsize}'/'${trace}'/output.tr' >> ${fname}

# CoDel
codelTarget=5ms
echo '"'${conf}' --queueDiscType=CoDel  --codelTarget='${codelTarget}' --diffServ=0",'${logprefix}'CoDel_0_'${codelTarget}${qsize}'/'${trace}'/output.tr' >> ${fname}

# RED
echo '"'${conf}' --queueDiscType=Red --diffServ=0",'${logprefix}'Hhf_0_'${qsize}'/'${trace}'/output.tr' >> ${fname}
