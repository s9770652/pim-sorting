#!/bin/bash

NR_TASKLETS=1
BLOCK_SIZE=16512
SEQREAD_CACHE_SIZE=1024

b=2
r=1000

cmd="NR_TASKLETS=${NR_TASKLETS} BLOCK_SIZE=${BLOCK_SIZE} SEQREAD_CACHE_SIZE=${SEQREAD_CACHE_SIZE} make all"

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
        rm obj/benchmark/quick_sorts.o bin/quick_sorts obj/host/app.o
        eval "QUICK_THRESHOLD=${threshold} TYPE=UINT${type} RECURSIVE=${recursive} ${cmd}"

        bin/host -b ${b} -r ${r} -t 3 | tee ${folder}/${threshold}.txt
    done
done
