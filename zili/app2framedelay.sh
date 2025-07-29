for aqm in `ls $1`
do
    for trace in `ls $1/${aqm}`
    do
        awk -f statapp.awk $1/${aqm}/${trace}/app.tr > $1/${aqm}/${trace}/framedelay.tr
    done
done