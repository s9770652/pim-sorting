HOST_DIR := host
DPU_DIR := dpu
BENCHMARK_DIR := benchmark
BUILD_DIR ?= bin
OBJ_DIR ?= obj

TYPE ?= UINT32
BLOCK_SIZE ?= 2048
SEQREAD_CACHE_SIZE ?= 256
NR_DPUS ?= 1
NR_TASKLETS ?= 1
PERF ?= 1
CHECK_SANITY ?= 1

define conf_filename
	${BUILD_DIR}/.NR_DPUS_${1}_NR_TASKLETS_${2}_TYPE_${3}_BLOCK_SIZE_${4}_SEQREAD_CACHE_SIZE_${5}.conf
endef
CONF := ${call conf_filename,${NR_DPUS},${NR_TASKLETS},${TYPE},${BLOCK_SIZE},${SEQREAD_CACHE_SIZE}}

HOST_TARGET := ${BUILD_DIR}/host
SORTING_TARGET := ${BUILD_DIR}/sorting
BENCHMARK_TARGET := ${BUILD_DIR}/benchmark

COMMON_INCLUDES := support
HOST_SRC := ${wildcard ${HOST_DIR}/*.c}
DPU_SRC := ${filter-out %/task.c, ${wildcard ${DPU_DIR}/*.c}}
SORTING_SRC := ${DPU_DIR}/task.c
BENCHMARK_SRC := ${wildcard ${BENCHMARK_DIR}/*.c}

HOST_OBJ := ${patsubst ${HOST_DIR}/%.c,${OBJ_DIR}/${HOST_DIR}/%.o,${HOST_SRC}}
DPU_OBJ := ${patsubst ${DPU_DIR}/%.c,${OBJ_DIR}/${DPU_DIR}/%.o,${DPU_SRC}}
SORTING_OBJ := ${patsubst ${DPU_DIR}/%.c,${OBJ_DIR}/${DPU_DIR}/%.o,${SORTING_SRC}}
BENCHMARK_OBJ := ${patsubst ${BENCHMARK_DIR}/%.c,${OBJ_DIR}/${BENCHMARK_DIR}/%.o,${BENCHMARK_SRC}}

.PHONY: all clean run

__dirs := ${shell mkdir -p ${BUILD_DIR} ${OBJ_DIR}/${HOST_DIR} ${OBJ_DIR}/${BENCHMARK_DIR} ${OBJ_DIR}/${DPU_DIR}}

COMMON_FLAGS := -Wall -Wextra -g -I${COMMON_INCLUDES}
HOST_FLAGS := ${COMMON_FLAGS} -std=c11 -O3 `dpu-pkg-config --cflags --libs dpu` \
	-DNR_TASKLETS=${NR_TASKLETS} \
	-DNR_DPUS=${NR_DPUS} \
	-DBLOCK_SIZE=${BLOCK_SIZE} \
	-D${TYPE} \
	-DSORTING_BINARY=\"./${SORTING_TARGET}\" \
	-DBENCHMARK_BINARY=\"./${BENCHMARK_TARGET}\"
DPU_FLAGS := ${COMMON_FLAGS} \
	-DNR_TASKLETS=${NR_TASKLETS} \
	-DBLOCK_SIZE=${BLOCK_SIZE} \
	-D${TYPE} \
	-DSEQREAD_CACHE_SIZE=${SEQREAD_CACHE_SIZE} \
	-DPERF=${PERF} \
	-DCHECK_SANITY=${CHECK_SANITY} \
	-DSTACK_SIZE_DEFAULT=768
SORTING_FLAGS := ${DPU_FLAGS} -O3
BENCHMARK_FLAGS := ${DPU_FLAGS} -Idpu -O3

all: ${CONF} ${HOST_TARGET} ${SORTING_TARGET} ${BENCHMARK_TARGET}

${CONF}:
	${RM} ${call conf_filename,*,*}
	touch ${CONF}

${HOST_TARGET}: ${HOST_OBJ} ${COMMON_INCLUDES}
	${CC} -o $@ ${HOST_OBJ} ${HOST_FLAGS}

${SORTING_TARGET}: ${SORTING_OBJ} ${DPU_OBJ} ${COMMON_INCLUDES}
	dpu-upmem-dpurte-clang ${SORTING_FLAGS} -o $@ ${SORTING_OBJ} ${DPU_OBJ}

${BENCHMARK_TARGET}: ${BENCHMARK_OBJ} ${DPU_OBJ} ${COMMON_INCLUDES}
	dpu-upmem-dpurte-clang ${BENCHMARK_FLAGS} -o $@ ${BENCHMARK_OBJ} ${DPU_OBJ}

${OBJ_DIR}/${HOST_DIR}/%.o: ${HOST_DIR}/%.c
	${CC} -c -o $@ $< ${HOST_FLAGS}

${OBJ_DIR}/${DPU_DIR}/%.o: ${DPU_DIR}/%.c
	dpu-upmem-dpurte-clang ${SORTING_FLAGS} -c -o $@ $<

${OBJ_DIR}/${BENCHMARK_DIR}/%.o: ${BENCHMARK_DIR}/%.c
	dpu-upmem-dpurte-clang ${BENCHMARK_FLAGS} -c -o $@ $<

clean:
	${RM} -r ${BUILD_DIR}
	${RM} -r ${OBJ_DIR}

run: all
	./${HOST_TARGET}
