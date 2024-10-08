#!/bin/bash

b=6
r=10
n=0x800000

main_folder=scripts/merge_mram
mkdir -p ${main_folder}

for nr_tasklets in 11 12 16
do
    tasklets_folder=${main_folder}/NR_TASKLETS=${nr_tasklets}
    for cache_size in 256 512 1024
    do
        cache_folder=${tasklets_folder}/CACHE_SIZE=${cache_size}
        for seqread_cache_size in 128 256 512
        do
            seqread_folder=${cache_folder}/SEQREAD_CACHE_SIZE=${seqread_cache_size}
            for type in 32 64
            do
                make clean
                NR_TASKLETS=${nr_tasklets} CACHE_SIZE=${cache_size} SEQREAD_CACHE_SIZE=${seqread_cache_size} TYPE=UINT${type} CHECK_SANITY=false make all

                folder=${seqread_folder}/uint${type}
                mkdir -p ${folder}

                dists=("sorted" "reverse" "almost" "zeroone" "uniform" "zipf")
                for dist in "${!dists[@]}";
                do
                    bin/host -b ${b} -r ${r} -t ${dist} -n $((n / type * 32)) | tee ${folder}/${dists[${dist}]}.txt
                done
            done
        done
    done
done
