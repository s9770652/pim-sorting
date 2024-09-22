#!/bin/bash

NR_TASKLETS=1
CACHE_SIZE=57368
SEQREAD_CACHE_SIZE=1024
MERGE_THRESHOLD=14

b=2
r=1000
n=""

main_folder=scripts/merge
mkdir -p ${main_folder}

n="$((${MERGE_THRESHOLD})),$((${MERGE_THRESHOLD} + 1))"
curr_n=${MERGE_THRESHOLD}
while [ $(( ${curr_n} + 1 )) -le 1024 ]
do
    n="${n},$((2 * ${curr_n})),$((2 * ${curr_n} + 1))"
    curr_n=$((2 * ${curr_n}))
done

for type in 32 64
do
    make clean
    NR_TASKLETS=${NR_TASKLETS} CACHE_SIZE=${CACHE_SIZE} SEQREAD_CACHE_SIZE=${SEQREAD_CACHE_SIZE} TYPE=UINT${type} make all

    folder=${main_folder}/uint${type}
    mkdir -p ${folder}

    dists=("sorted" "reverse" "almost" "zeroone" "uniform" "zipf")
    for dist in "${!dists[@]}";
    do
        bin/host -b ${b} -r ${r} -t ${dist} -n ${n} | tee ${folder}/${dists[${dist}]}.txt
    done
done
