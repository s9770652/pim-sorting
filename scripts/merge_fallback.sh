#!/bin/bash

NR_TASKLETS=1
BLOCK_SIZE=50000
SEQREAD_CACHE_SIZE=1024

b=3
r=1
n=""

main_folder=scripts/merge
mkdir -p ${main_folder}

get_n() {
    n="$(( ${1} + 1 ))"
    curr_n=${1}
    while [ $(( ${curr_n} + 1 )) -le 1024 ]
    do
        n="${n},$((4 * ${curr_n} + 1))"
        curr_n=$(( 4 * ${curr_n} ))
    done
}

for type in 32 64
do
    make clean
    NR_TASKLETS=${NR_TASKLETS} BLOCK_SIZE=${BLOCK_SIZE} SEQREAD_CACHE_SIZE=${SEQREAD_CACHE_SIZE} TYPE=UINT${type} make all
    for threshold in 16 17 18 24 32 48 64 96
    do
        folder=${main_folder}/threshold=${threshold}/uint${type}
        mkdir -p ${folder}

        rm obj/benchmark/wram_sorts.o bin/wram_sorts obj/host/app.o
        NR_TASKLETS=${NR_TASKLETS} BLOCK_SIZE=${BLOCK_SIZE} SEQREAD_CACHE_SIZE=${SEQREAD_CACHE_SIZE} TYPE=UINT${type} MERGE_THRESHOLD=${threshold} make all

        get_n ${threshold}

        dists=("sorted" "reverse" "almost" "uniform" "zipf" "normal")
        for dist in "${!dists[@]}";
        do
            bin/host -b ${b} -r ${r} -t ${dist} -n ${n} | tee ${folder}/${dists[${dist}]}.txt
        done
    done
done
