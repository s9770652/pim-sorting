#!/bin/bash

NR_TASKLETS=1
CACHE_SIZE=16512
SEQREAD_CACHE_SIZE=1024

b=0
r=1000
n="3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24"

main_folder="scripts/small sorts"
mkdir -p "${main_folder}"

for type in 32 64
do
    make clean
    NR_TASKLETS=${NR_TASKLETS} CACHE_SIZE=${CACHE_SIZE} SEQREAD_CACHE_SIZE=${SEQREAD_CACHE_SIZE} TYPE=UINT${type} make all

    folder=${main_folder}/uint${type}
    mkdir -p "${folder}"

    dists=("sorted" "reverse" "almost" "uniform" "zipf" "normal")
    for dist in "${!dists[@]}";
    do
        bin/host -b ${b} -r ${r} -t ${dist} -n ${n} | tee "${folder}/${dists[${dist}]}.txt"
    done
done
