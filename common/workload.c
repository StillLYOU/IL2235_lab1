/**
 * @file workload.c
 * @author Matthias Becker (mabecker@kth.se)
 * @brief Implements the workload used. 
 * @version 0.1
 * @date 2025-10-17
 * 
 * @copyright Copyright (c) 2025
 */
#include <stdio.h>
#include <inttypes.h>
#include "bsp.h" 
#include "workload.h"

void job_A(jobReturn_t* retval) {
    retval->start = time_us_64();

    BSP_WaitClkCycles(EXECUTION_TIME_A);

    retval->stop = time_us_64();
}
/*-----------------------------------------------------------*/

void job_B(jobReturn_t* retval) {
    retval->start = time_us_64();

    BSP_WaitClkCycles(EXECUTION_TIME_B);

    retval->stop = time_us_64();
}
/*-----------------------------------------------------------*/

void job_C(jobReturn_t* retval) {
    retval->start = time_us_64();

    // Read GPIO switches to determine delay time
    bool bit7 = BSP_GetInput(SW_10);  /* SW_10 - MSB */
    bool bit6 = BSP_GetInput(SW_11);  /* SW_11 */
    bool bit5 = BSP_GetInput(SW_12);  /* SW_12 */
    bool bit4 = BSP_GetInput(SW_13);  /* SW_13 */
    bool bit3 = BSP_GetInput(SW_14);  /* SW_14 */
    bool bit2 = BSP_GetInput(SW_15);  /* SW_15 */
    bool bit1 = BSP_GetInput(SW_16);  /* SW_16 */
    bool bit0 = BSP_GetInput(SW_17);  /* SW_17 - LSB */

    // Construct 8-bit value from switches (0-255)
    uint8_t switch_value = (bit7 << 7) | (bit6 << 6) | (bit5 << 5) | (bit4 << 4) |
                           (bit3 << 3) | (bit2 << 2) | (bit1 << 1) | bit0;

    // Calculate delay time: map 0-255 to 0-8000us, then subtract 10us
    // (switch_value * 8000 / 256) - 10 = (switch_value * 31.25) - 10
    uint32_t delay_us = ((switch_value * 8000) / 256) - 10;
    uint32_t delay_cycles = delay_us * CYCLES_PER_US;

    BSP_WaitClkCycles(delay_cycles);

    retval->stop = time_us_64();
}
/*-----------------------------------------------------------*/

void job_D(jobReturn_t* retval) {
    retval->start = time_us_64();

    BSP_WaitClkCycles(EXECUTION_TIME_D);

    retval->stop = time_us_64();
}
/*-----------------------------------------------------------*/

void job_E(jobReturn_t* retval) {
    retval->start = time_us_64();

    BSP_WaitClkCycles(EXECUTION_TIME_E);

    retval->stop = time_us_64();
}
/*-----------------------------------------------------------*/

void job_F(jobReturn_t* retval) {
    retval->start = time_us_64();

    BSP_WaitClkCycles(EXECUTION_TIME_F);

    retval->stop = time_us_64();
}
/*-----------------------------------------------------------*/