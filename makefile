# Folder structure.
HOST_DIR := host
DPU_DIR := dpu
BENCHMARK_DIR := bench_*
BUILD_DIR ?= bin
OBJ_DIR ?= obj

__dirs := ${shell mkdir -p ${BUILD_DIR} ${OBJ_DIR}/${HOST_DIR} ${OBJ_DIR}/${BENCHMARK_DIR} ${OBJ_DIR}/${DPU_DIR}}

# Compilation constants.
TYPE ?= UINT32
CACHE_SIZE ?= 1024
SEQREAD_CACHE_SIZE ?= 512
NR_DPUS ?= 1
NR_TASKLETS ?= 16
CHECK_SANITY ?= 0

QUICK_THRESHOLD ?= 18
PIVOT ?= MEDIAN_OF_RANDOM
PARTITION_PRIO ?= RIGHT
RECURSIVE ?= false
MERGE_THRESHOLD ?= 14
STRAIGHT_READER ?= READ_OPT
STABLE ?= true

# A file whose name reflects the set constants.
define conf_filename
	${BUILD_DIR}/.NR_DPUS_${1}_NR_TASKLETS_${2}_TYPE_${3}_CACHE_SIZE_${4}_SEQREAD_CACHE_SIZE_${5}.conf
endef
CONF := ${call conf_filename,${NR_DPUS},${NR_TASKLETS},${TYPE},${CACHE_SIZE},${SEQREAD_CACHE_SIZE}}

# The list (as string) of all binaries created. Needed by the host.
comma := ,
empty :=
space := ${empty} ${empty}
BENCHMARKS := small_wram quick_wram merge_wram heap_wram merge_mram_hs merge_mram_hs_custom merge_mram_fs merge_par
BINARIES := ${patsubst %,./${BUILD_DIR}/%,${BENCHMARKS}}
BINARIES := ${subst ${space},${comma},${BINARIES}}

# The actual binaries to build.
HOST_TARGET := ${BUILD_DIR}/host
BENCHMARK_TARGETS := ${patsubst %,${BUILD_DIR}/%,${BENCHMARKS}}

# On which source files the binaries depend.
COMMON_INCLUDES := support
HOST_SRC := ${wildcard ${HOST_DIR}/*.c}
DPU_SRC := ${wildcard ${DPU_DIR}/*.c}
BENCHMARK_SRC :=

# The object files build from each of the source files.
HOST_OBJ := ${patsubst ${HOST_DIR}/%.c,${OBJ_DIR}/${HOST_DIR}/%.o,${HOST_SRC}}
DPU_OBJ := ${patsubst ${DPU_DIR}/%.c,${OBJ_DIR}/${DPU_DIR}/%.o,${DPU_SRC}}
BENCHMARK_OBJ := ${patsubst ${BENCHMARK_DIR}/%.c,${OBJ_DIR}/${BENCHMARK_DIR}/%.o,${BENCHMARK_SRC}}

# The compilation flags.
COMMON_FLAGS := -Wall -Wextra -g -I${COMMON_INCLUDES}
HOST_FLAGS := ${COMMON_FLAGS} -std=c11 -lm -O3 `dpu-pkg-config --cflags --libs dpu` \
	-DNR_TASKLETS=${NR_TASKLETS} \
	-DNR_DPUS=${NR_DPUS} \
	-DCACHE_SIZE=${CACHE_SIZE} \
	-D${TYPE} \
	-DSEQREAD_CACHE_SIZE=${SEQREAD_CACHE_SIZE} \
	-DCHECK_SANITY=${CHECK_SANITY} \
	-DBINARIES=\"${BINARIES}\" \
	-DTABLE_HEADER=\"QUICK_THRESHOLD=${QUICK_THRESHOLD},\ \
	PIVOT=${PIVOT},\ \
	PARTITION_PRIO=${PARTITION_PRIO},\ \
	RECURSIVE=${RECURSIVE},\ \
	MERGE_THRESHOLD=${MERGE_THRESHOLD},\ \
	STRAIGHT_READER=${STRAIGHT_READER},\ \
	STABLE=${STABLE}\"
DPU_FLAGS := ${COMMON_FLAGS} -O3 \
	-DNR_TASKLETS=${NR_TASKLETS} \
	-DCACHE_SIZE=${CACHE_SIZE} \
	-D${TYPE} \
	-DSEQREAD_CACHE_SIZE=${SEQREAD_CACHE_SIZE} \
	-D${PIVOT} \
	-DCHECK_SANITY=${CHECK_SANITY} \
	-DSTRAIGHT_READER=${STRAIGHT_READER} \
	-DSTABLE=${STABLE}
BENCHMARK_FLAGS := ${DPU_FLAGS} -Idpu -DSTACK_SIZE_DEFAULT=600 \
	-DPARTITION_PRIO=${PARTITION_PRIO} \
	-DQUICK_THRESHOLD=${QUICK_THRESHOLD} \
	-DMERGE_THRESHOLD=${MERGE_THRESHOLD} \
	-DRECURSIVE=${RECURSIVE}

.PHONY: all clean run
.PRECIOUS: ${OBJ_DIR}/${BENCHMARK_DIR}/%.o

all: ${CONF} ${HOST_TARGET} ${BENCHMARK_TARGETS}

clean:
	${RM} -r ${BUILD_DIR}
	${RM} -r ${OBJ_DIR}

run: all
	./${HOST_TARGET}

# Rules.
${CONF}:
	${RM} ${call conf_filename,*,*}
	touch ${CONF}

${HOST_TARGET}: ${HOST_OBJ} ${COMMON_INCLUDES}
	${CC} -o $@ ${HOST_OBJ} ${HOST_FLAGS}

${BUILD_DIR}/%: ${OBJ_DIR}/${BENCHMARK_DIR}/%.o ${BENCHMARK_OBJ} ${DPU_OBJ} ${COMMON_INCLUDES}
	dpu-upmem-dpurte-clang ${BENCHMARK_FLAGS} -o $@ $< ${BENCHMARK_OBJ} ${DPU_OBJ}

${OBJ_DIR}/${HOST_DIR}/%.o: ${HOST_DIR}/%.c
	${CC} -c -o $@ $< ${HOST_FLAGS}

${OBJ_DIR}/${DPU_DIR}/%.o: ${DPU_DIR}/%.c
	dpu-upmem-dpurte-clang ${DPU_FLAGS} -c -o $@ $<

${OBJ_DIR}/${BENCHMARK_DIR}/%.o: ${BENCHMARK_DIR}/%.c
	dpu-upmem-dpurte-clang ${BENCHMARK_FLAGS} -c -o $@ $<
