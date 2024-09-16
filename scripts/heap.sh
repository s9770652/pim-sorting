#!/bin/bash

NR_TASKLETS=1
CACHE_SIZE=16512
SEQREAD_CACHE_SIZE=1024

b=3
r=1000
n="16,24,32,48,64,96,128,192,256,384,512,768,1024"

main_folder=scripts/heap
mkdir -p ${main_folder}

for type in 32 64
do
    make clean
    NR_TASKLETS=${NR_TASKLETS} CACHE_SIZE=${CACHE_SIZE} SEQREAD_CACHE_SIZE=${SEQREAD_CACHE_SIZE} TYPE=UINT${type} make all

    folder=${main_folder}/uint${type}
    mkdir -p ${folder}

    dists=("sorted" "reverse" "almost" "uniform" "zipf" "normal")
    for dist in "${!dists[@]}";
    do
        bin/host -b ${b} -r ${r} -t ${dist} -n ${n} | tee ${folder}/${dists[${dist}]}.txt
    done
done
