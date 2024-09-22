#!/bin/bash

NR_TASKLETS=1
CACHE_SIZE=16512
SEQREAD_CACHE_SIZE=1024

b=1
r=1000
n="16,24,32,48,64,96,128,192,256,384,512,768,1024"

cmd="NR_TASKLETS=${NR_TASKLETS} CACHE_SIZE=${CACHE_SIZE} SEQREAD_CACHE_SIZE=${SEQREAD_CACHE_SIZE} make all"

main_folder=scripts/quick/fallback
mkdir -p ${main_folder}

for type in 32 64
do
    make clean
    eval "TYPE=UINT${type} ${cmd}"

    folder=${main_folder}/uint${type}
    mkdir -p ${folder}

    recursive=false
    if [ ${type} -eq 64 ];
    then
        recursive=true
    fi

    for threshold in {11..20}
    do
        rm obj/bench_*/quick_wram.o bin/quick_wram obj/host/app.o
        eval "QUICK_THRESHOLD=${threshold} TYPE=UINT${type} RECURSIVE=${recursive} ${cmd}"

        bin/host -b ${b} -r ${r} -t 4 -n ${n} | tee ${folder}/${threshold}.txt
    done
done
