#!/bin/bash

CACHE_SIZE=1024
SEQREAD_CACHE_SIZE=512

b=7
r=10
n=0x800000

main_folder=scripts/merge_par
mkdir -p ${main_folder}

for nr_tasklets in 1 2 4 8 16
do
    tasklets_folder=${main_folder}/NR_TASKLETS=${nr_tasklets}
    for stable in false
    do
        stable_folder=${tasklets_folder}/STABLE=${stable}
        for type in 32 64
        do
            make clean
            NR_TASKLETS=${nr_tasklets} CACHE_SIZE=${CACHE_SIZE} SEQREAD_CACHE_SIZE=${SEQREAD_CACHE_SIZE} TYPE=UINT${type} CHECK_SANITY=false STABLE=${stable} make all

            folder=${stable_folder}/uint${type}
            mkdir -p ${folder}

            dists=("sorted" "reverse" "almost" "zeroone" "uniform" "zipf")
            for dist in "${!dists[@]}";
            do
                bin/host -b ${b} -r ${r} -t ${dist} -n $((n / type * 32)) | tee ${folder}/${dists[${dist}]}.txt
            done
        done
    done
done
