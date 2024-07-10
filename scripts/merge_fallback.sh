#!/bin/bash

NR_TASKLETS=1
BLOCK_SIZE=16512
SEQREAD_CACHE_SIZE=1024

b=3
r=1000

main_folder=scripts/merge
mkdir -p ${main_folder}

for type in 32 64
do
    make clean
    NR_TASKLETS=${NR_TASKLETS} BLOCK_SIZE=${BLOCK_SIZE} SEQREAD_CACHE_SIZE=${SEQREAD_CACHE_SIZE} TYPE=UINT${type} make all
    for fallback in 16 24 32 48 64 96
    do
        folder=${main_folder}/fallback=${fallback}/uint${type}
        mkdir -p ${folder}

        rm obj/benchmark/wram_sorts.o bin/wram_sorts
        NR_TASKLETS=${NR_TASKLETS} BLOCK_SIZE=${BLOCK_SIZE} SEQREAD_CACHE_SIZE=${SEQREAD_CACHE_SIZE} TYPE=UINT${type} MERGE_TO_SHELL=${fallback} make all

        dists=("sorted" "reverse" "almost" "uniform" "zipf" "normal")
        for dist in "${!dists[@]}";
        do
            bin/host -b ${b} -r ${r} -t ${dist} | tee ${folder}/${dists[${dist}]}.txt
        done
    done
done
