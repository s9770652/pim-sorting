#!/bin/bash

NR_TASKLETS=1
CACHE_SIZE=57368
SEQREAD_CACHE_SIZE=1024

b=3
r=1000
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
    NR_TASKLETS=${NR_TASKLETS} CACHE_SIZE=${CACHE_SIZE} SEQREAD_CACHE_SIZE=${SEQREAD_CACHE_SIZE} TYPE=UINT${type} make all
    for threshold in 12 13 14 15 16 24 32 48 64 96
    do
        folder=${main_folder}/threshold=${threshold}/uint${type}
        mkdir -p ${folder}

        rm obj/bench_*/merge_wram.o bin/merge_wram obj/host/app.o
        NR_TASKLETS=${NR_TASKLETS} CACHE_SIZE=${CACHE_SIZE} SEQREAD_CACHE_SIZE=${SEQREAD_CACHE_SIZE} TYPE=UINT${type} MERGE_THRESHOLD=${threshold} make all

        get_n ${threshold}

        dists=("sorted" "reverse" "almost" "uniform" "zipf" "normal")
        for dist in "${!dists[@]}";
        do
            bin/host -b ${b} -r ${r} -t ${dist} -n ${n} | tee ${folder}/${dists[${dist}]}.txt
        done
    done
done
