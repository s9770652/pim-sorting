#!/bin/bash

NR_TASKLETS=1
BLOCK_SIZE=16512
SEQREAD_CACHE_SIZE=1024

b=1
r=1000

main_folder=scripts/shell/ciura
mkdir -p ${main_folder}

for type in 32 64
do
    make clean
    NR_TASKLETS=${NR_TASKLETS} BLOCK_SIZE=${BLOCK_SIZE} SEQREAD_CACHE_SIZE=${SEQREAD_CACHE_SIZE} TYPE=UINT${type} make all

    folder=${main_folder}/uint${type}
    mkdir -p ${folder}

    dists=("sorted" "reverse" "almost" "uniform" "zipf" "normal")
    for dist in "${!dists[@]}";
    do
        bin/host -b ${b} -r ${r} -t ${dist} | tee ${folder}/${dists[${dist}]}.txt
    done
done
