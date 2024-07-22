#!/bin/bash

NR_TASKLETS=1
BLOCK_SIZE=16512
SEQREAD_CACHE_SIZE=1024
QUICK_TO_INSERTION=13
TYPE=32

b=2
r=1000

cmd="NR_TASKLETS=${NR_TASKLETS} BLOCK_SIZE=${BLOCK_SIZE} SEQREAD_CACHE_SIZE=${SEQREAD_CACHE_SIZE} QUICK_TO_INSERTION=${QUICK_TO_INSERTION} TYPE=UINT${TYPE} make all"

main_folder=scripts/quick/matrix
mkdir -p ${main_folder}

make clean
eval cmd
for way in 0 1
do
    way_name=("iterative" "recursive")
    way_folder=${main_folder}/${way_name[${way}]}
    mkdir -p ${way_folder}
    for pivot in "LAST" "MIDDLE" "MEDIAN" "RANDOM" "MEDIAN_OF_RANDOM"
    do
        pivot_folder=${way_folder}/${pivot}
        mkdir -p ${pivot_folder}
        for prio in 1 2 3
        do
            prio_name=("shorter" "left" "right")
            prio_folder=${pivot_folder}/${prio_name[${prio}-1]}
            mkdir -p ${prio_folder}

            rm obj/benchmark/quick_sorts.o bin/quick_sorts
            eval "RECURSIVE=${way} PIVOT=${pivot} PARTITION_PRIO=${prio} ${cmd}"

            bin/host -b ${b} -r ${r} -t 3 | tee ${prio_folder}/uniform.txt
        done
    done
done
