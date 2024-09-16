#!/bin/bash

CACHE_SIZE=1024
SEQREAD_CACHE_SIZE=512
NR_TASKLETS=16

b=4
r=10
n=0x800000

main_folder=scripts/merge_mram/half-space
mkdir -p ${main_folder}

for type in 32 64
do
    make clean
    NR_TASKLETS=${NR_TASKLETS} CACHE_SIZE=${CACHE_SIZE} SEQREAD_CACHE_SIZE=${SEQREAD_CACHE_SIZE} TYPE=UINT${type} CHECK_SANITY=false make all

    folder=${main_folder}/uint${type}
    mkdir -p ${folder}

    dists=("sorted" "reverse" "almost" "uniform" "zipf" "normal")
    for dist in "${!dists[@]}";
    do
        bin/host -b ${b} -r ${r} -t ${dist} -n $((n / type * 32)) | tee ${folder}/${dists[${dist}]}.txt
    done
done
