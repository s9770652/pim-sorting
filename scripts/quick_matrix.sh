#!/bin/bash

NR_TASKLETS=1
BLOCK_SIZE=16512
SEQREAD_CACHE_SIZE=1024

b=2
r=1000

cmd="NR_TASKLETS=${NR_TASKLETS} BLOCK_SIZE=${BLOCK_SIZE} SEQREAD_CACHE_SIZE=${SEQREAD_CACHE_SIZE} make all"

main_folder=scripts/quick/matrix
mkdir -p ${main_folder}

for type in 32 64
do
    make clean
    eval "TYPE=UINT${type} ${cmd}"
    for way in false true
    do
        way_name="iterative"
        if [ ${way} = true ];
        then
            way_name="recursive"
        fi
        way_folder=${main_folder}/${way_name}
        mkdir -p ${way_folder}

        for pivot in "LAST" "MEDIAN" "RANDOM" "MEDIAN_OF_RANDOM"
        do
            pivot_folder=${way_folder}/${pivot,,}
            mkdir -p ${pivot_folder}
            for prio in "SHORTER" "LEFT" "RIGHT"
            do
                prio_folder=${pivot_folder}/${prio,,}/uint${type}
                mkdir -p ${prio_folder}

                rm obj/benchmark/quick_sorts.o bin/quick_sorts obj/host/app.o
                eval "RECURSIVE=${way} PIVOT=${pivot} PARTITION_PRIO=${prio} TYPE=UINT${type} ${cmd}"

                dists=("sorted" "reverse" "almost" "uniform" "zipf" "normal")
                for dist in "${!dists[@]}";
                do
                    if [ "$pivot" = "LAST" ] && [ ${dist} -lt 3 ];
                    then
                        continue
                    fi
                    bin/host -b ${b} -r ${r} -t ${dist} | tee ${prio_folder}/${dists[${dist}]}.txt
                done
            done
        done
    done
done
