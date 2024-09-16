/**
 * @file
 * @brief Provides mathematical functions for usage by DPUs.
**/

#ifndef _DPU_MATH_H_
#define _DPU_MATH_H_

/**
 * @brief An approximation of the square root using the Babylonian method.
 * This function is needed as `math.h` is not provided for DPUs.
 * 
 * @param square The number of which to take the root.
 * 
 * @return The approximation of the square root after 16 iterations.
**/
static inline float sqroot_on_dpu(float const square) {
    float root = square / 3, prev_root;
    if (square <= 0) return 0;
    do {
        prev_root = root;
        root = (root + square / root) / 2;
    } while (root - prev_root > 1 || root - prev_root < -1);
    return root;
}

#endif  // _DPU_MATH_H_
