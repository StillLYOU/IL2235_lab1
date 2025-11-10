#include <stdio.h>
#include "bsp.h"
#include "workload.h"

/*************************************************************/

/* Cyclic scheduler parameters */
#define MINOR_FRAME_MS 5      /* Minor frame duration: 5ms */
#define HYPERPERIOD_MS 100    /* Hyperperiod: 100ms */
#define NUM_FRAMES 20         /* Number of frames in hyperperiod */
#define MAX_TASKS_PER_FRAME 4 /* Maximum tasks in a single frame */
#define MAX_JOBS_PER_HYPERPERIOD 50  /* Maximum job executions per hyperperiod */

/* Task function pointer type */
typedef void (*task_func_t)(jobReturn_t*);

/* Frame schedule structure - defines which tasks run in each frame */
typedef struct {
    task_func_t tasks[MAX_TASKS_PER_FRAME];  /* Task functions to execute */
    const char* names[MAX_TASKS_PER_FRAME];   /* Task names for logging */
    uint8_t num_tasks;                         /* Number of tasks in this frame */
} frame_schedule_t;

/* Job execution record */
typedef struct {
    uint32_t frame;          /* Frame number */
    const char* task_name;   /* Task name */
    uint64_t release_time;   /* Release time (frame start) */
    uint64_t start_time;     /* Actual execution start time */
    uint64_t completion_time; /* Completion time */
    uint64_t exec_time;      /* Execution time */
    uint64_t deadline;       /* Absolute deadline for this job */
    bool deadline_missed;    /* Flag indicating if deadline was missed */
} job_record_t;

/* Global variables */
static uint32_t current_frame = 0;
static repeating_timer_t frame_timer;
static job_record_t job_log[MAX_JOBS_PER_HYPERPERIOD];
static uint32_t job_count = 0;
static uint32_t hyperperiod_count = 0;
static uint64_t scheduler_start_time = 0;  /* Absolute start time of scheduler */

/* Deadline miss tracking */
static uint32_t deadline_misses_current = 0;  /* Misses in current hyperperiod */
static uint32_t deadline_misses_total = 0;    /* Total misses since start */

/* Static schedule table for the hyperperiod (20 frames)
 * Custom cyclic schedule pattern:
 * BAD, BF, BA, BC, BAF, BC, BA, BE, BAF, B, BAD, BC, BAF, BD, BA, BC, BAF, BE, BA, B
 *
 * Execution pattern per hyperperiod:
 *   A: frames 0,2,4,6,8,10,12,14,16,18 (10 times)
 *   B: frames 0-19 (20 times, every frame) - ALWAYS FIRST
 *   C: frames 3,5,11,15 (4 times)
 *   D: frames 0,10,13 (3 times)
 *   E: frames 7,17 (2 times)
 *   F: frames 1,4,8,12,16 (5 times)
 */
static frame_schedule_t schedule[NUM_FRAMES] = {
    /* Frame 0 (0ms): B, A, D | Load: 1+1+2=4ms */
    { .tasks = {job_B, job_A, job_D}, .names = {"Task_B", "Task_A", "Task_D"}, .num_tasks = 3 },
    /* Frame 1 (5ms): B, F | Load: 1+2=3ms */
    { .tasks = {job_B, job_F}, .names = {"Task_B", "Task_F"}, .num_tasks = 2 },
    /* Frame 2 (10ms): B, A | Load: 1+1=2ms */
    { .tasks = {job_B, job_A}, .names = {"Task_B", "Task_A"}, .num_tasks = 2 },
    /* Frame 3 (15ms): B, C | Load: 1+C (C is variable via GPIO) */
    { .tasks = {job_B, job_C}, .names = {"Task_B", "Task_C"}, .num_tasks = 2 },
    /* Frame 4 (20ms): B, A, F | Load: 1+1+2=4ms */
    { .tasks = {job_B, job_A, job_F}, .names = {"Task_B", "Task_A", "Task_F"}, .num_tasks = 3 },
    /* Frame 5 (25ms): B, C | Load: 1+C (C is variable via GPIO) */
    { .tasks = {job_B, job_C}, .names = {"Task_B", "Task_C"}, .num_tasks = 2 },
    /* Frame 6 (30ms): B, A | Load: 1+1=2ms */
    { .tasks = {job_B, job_A}, .names = {"Task_B", "Task_A"}, .num_tasks = 2 },
    /* Frame 7 (35ms): B, E | Load: 1+4=5ms */
    { .tasks = {job_B, job_E}, .names = {"Task_B", "Task_E"}, .num_tasks = 2 },
    /* Frame 8 (40ms): B, A, F | Load: 1+1+2=4ms */
    { .tasks = {job_B, job_A, job_F}, .names = {"Task_B", "Task_A", "Task_F"}, .num_tasks = 3 },
    /* Frame 9 (45ms): B | Load: 1ms */
    { .tasks = {job_B}, .names = {"Task_B"}, .num_tasks = 1 },
    /* Frame 10 (50ms): B, A, D | Load: 1+1+2=4ms */
    { .tasks = {job_B, job_A, job_D}, .names = {"Task_B", "Task_A", "Task_D"}, .num_tasks = 3 },
    /* Frame 11 (55ms): B, C | Load: 1+C (C is variable via GPIO) */
    { .tasks = {job_B, job_C}, .names = {"Task_B", "Task_C"}, .num_tasks = 2 },
    /* Frame 12 (60ms): B, A, F | Load: 1+1+2=4ms */
    { .tasks = {job_B, job_A, job_F}, .names = {"Task_B", "Task_A", "Task_F"}, .num_tasks = 3 },
    /* Frame 13 (65ms): B, D | Load: 1+2=3ms */
    { .tasks = {job_B, job_D}, .names = {"Task_B", "Task_D"}, .num_tasks = 2 },
    /* Frame 14 (70ms): B, A | Load: 1+1=2ms */
    { .tasks = {job_B, job_A}, .names = {"Task_B", "Task_A"}, .num_tasks = 2 },
    /* Frame 15 (75ms): B, C | Load: 1+C (C is variable via GPIO) */
    { .tasks = {job_B, job_C}, .names = {"Task_B", "Task_C"}, .num_tasks = 2 },
    /* Frame 16 (80ms): B, A, F | Load: 1+1+2=4ms */
    { .tasks = {job_B, job_A, job_F}, .names = {"Task_B", "Task_A", "Task_F"}, .num_tasks = 3 },
    /* Frame 17 (85ms): B, E | Load: 1+4=5ms */
    { .tasks = {job_B, job_E}, .names = {"Task_B", "Task_E"}, .num_tasks = 2 },
    /* Frame 18 (90ms): B, A | Load: 1+1=2ms */
    { .tasks = {job_B, job_A}, .names = {"Task_B", "Task_A"}, .num_tasks = 2 },
    /* Frame 19 (95ms): B | Load: 1ms */
    { .tasks = {job_B}, .names = {"Task_B"}, .num_tasks = 1 }
};


/**
 * @brief Print all job executions from the last hyperperiod
 */
void print_hyperperiod_report(void) {
    printf("\n========== Hyperperiod %u Report ==========\n", hyperperiod_count);
    printf("Frame | Task   | Release    | Start      | Complete   | Deadline   | Exec Time | Status\n");
    printf("------+--------+------------+------------+------------+------------+-----------+---------\n");

    for (uint32_t i = 0; i < job_count; i++) {
        const char* status;
        if (job_log[i].exec_time == 0 && job_log[i].deadline_missed) {
            status = " SKIPPED";
        } else if (job_log[i].deadline_missed) {
            status = "  MISS  ";
        } else {
            status = "   OK   ";
        }

        printf(" %2u   | %-6s | %10llu | %10llu | %10llu | %10llu | %6llu us | %s\n",
               job_log[i].frame,
               job_log[i].task_name,
               job_log[i].release_time,
               job_log[i].start_time,
               job_log[i].completion_time,
               job_log[i].deadline,
               job_log[i].exec_time,
               status);
    }

    printf("========================================================================================\n");
    printf("Total jobs scheduled: %u\n", job_count);
    printf("Deadline misses (this hyperperiod): %u\n", deadline_misses_current);
    printf("Deadline misses (total): %u\n", deadline_misses_total);

    if (deadline_misses_current > 0) {
        printf("\n*** WARNING: Deadline misses detected! ***\n");
        printf("Response Strategy: SKIP TASK_C IF INSUFFICIENT TIME BEFORE EXECUTION\n");
    }
    printf("\n");
}

/**
 * @brief Frame timer callback - executes tasks for the current frame
 *
 * @param tmr Pointer to the repeating timer
 * @return true to keep timer running
 */
bool frame_callback(repeating_timer_t *tmr) {
    jobReturn_t result;
    uint64_t actual_time = time_us_64();
    uint32_t local_frame = current_frame % NUM_FRAMES;

    /* Initialize scheduler start time on first callback */
    if (current_frame == 0) {
        scheduler_start_time = actual_time;
    }

    /* Calculate absolute deadline based on scheduler start time, not actual callback time */
    uint64_t frame_start = scheduler_start_time + (current_frame * MINOR_FRAME_MS * 1000);
    uint64_t frame_deadline = frame_start + (MINOR_FRAME_MS * 1000);  /* Deadline in microseconds */

    /* Execute all tasks scheduled for this frame (in order) */
    for (uint8_t i = 0; i < schedule[local_frame].num_tasks; i++) {
        /* Special handling for Task_C: check if there's enough time before executing */
        if (schedule[local_frame].tasks[i] == job_C) {
            /* Read GPIO switches to get actual execution time for Task_C */
            bool bit7 = BSP_GetInput(SW_10);
            bool bit6 = BSP_GetInput(SW_11);
            bool bit5 = BSP_GetInput(SW_12);
            bool bit4 = BSP_GetInput(SW_13);
            bool bit3 = BSP_GetInput(SW_14);
            bool bit2 = BSP_GetInput(SW_15);
            bool bit1 = BSP_GetInput(SW_16);
            bool bit0 = BSP_GetInput(SW_17);

            uint8_t switch_value = (bit7 << 7) | (bit6 << 6) | (bit5 << 5) | (bit4 << 4) |
                                   (bit3 << 3) | (bit2 << 2) | (bit1 << 1) | bit0;

            /* Calculate Task_C's actual execution time from GPIO */
            uint32_t task_c_wcet_us = ((switch_value * 8000) / 256);  /* Note: actual execution is this - 10us, but we add margin */

            /* Check if there's enough time to complete Task_C */
            uint64_t current_time = time_us_64();
            int64_t time_remaining = (int64_t)frame_deadline - (int64_t)current_time;

            if (time_remaining < (int64_t)task_c_wcet_us) {
                /* Not enough time - skip Task_C */
                if (job_count < MAX_JOBS_PER_HYPERPERIOD) {
                    job_log[job_count].frame = local_frame;
                    job_log[job_count].task_name = schedule[local_frame].names[i];
                    job_log[job_count].release_time = frame_start;
                    job_log[job_count].start_time = 0;
                    job_log[job_count].completion_time = 0;
                    job_log[job_count].exec_time = 0;
                    job_log[job_count].deadline = frame_deadline;
                    job_log[job_count].deadline_missed = true;
                    job_count++;
                }

                deadline_misses_current++;
                deadline_misses_total++;

                /* LED indication */
                BSP_ToggleLED(LED_RED);

                /* Note: Skip printf here to avoid blocking the scheduler */

                continue;  /* Skip Task_C, move to next task */
            }
        }

        /* Execute the task */
        schedule[local_frame].tasks[i](&result);

        /* Record job execution (avoid buffer overflow) */
        if (job_count < MAX_JOBS_PER_HYPERPERIOD) {
            job_log[job_count].frame = local_frame;
            job_log[job_count].task_name = schedule[local_frame].names[i];
            job_log[job_count].release_time = frame_start;
            job_log[job_count].start_time = result.start;
            job_log[job_count].completion_time = result.stop;
            job_log[job_count].exec_time = result.stop - result.start;
            job_log[job_count].deadline = frame_deadline;
            job_log[job_count].deadline_missed = (result.stop > frame_deadline);
            job_count++;
        }

        /* Check for deadline miss after execution */
        if (result.stop > frame_deadline) {
            deadline_misses_current++;
            deadline_misses_total++;

            /* LED indication - Toggle red LED to signal error */
            BSP_ToggleLED(LED_RED);

            /* Note: Skip printf here to avoid blocking the scheduler */
        }
    }

    /* Move to next frame */
    current_frame++;

    /* Check if hyperperiod completed */
    if (current_frame % NUM_FRAMES == 0) {
        BSP_ToggleLED(LED_GREEN);

        /* Print report for completed hyperperiod */
        print_hyperperiod_report();

        /* Reset for next hyperperiod */
        job_count = 0;
        deadline_misses_current = 0;
        hyperperiod_count++;
    }

    return true;  /* Keep timer running */
}

/**
 * @brief Main function.
 *
 * @return int
 */
int main()
{
    BSP_Init();  /* Initialize all components on the lab-kit. */

    printf("\n========================================\n");
    printf("Cyclic Scheduler Started\n");
    printf("Minor Frame: %d ms\n", MINOR_FRAME_MS);
    printf("Hyperperiod: %d ms (%d frames)\n", HYPERPERIOD_MS, NUM_FRAMES);
    printf("========================================\n");
    printf("Schedule Preview:\n");
    for (int i = 0; i < NUM_FRAMES; i++) {
        printf("  F%02d: ", i);
        for (int j = 0; j < schedule[i].num_tasks; j++) {
            printf("%s", schedule[i].names[j]);
            if (j < schedule[i].num_tasks - 1) printf(", ");
        }
        printf("\n");
    }
    printf("========================================\n");
    printf("Collecting data... Reports printed every %d ms\n\n", HYPERPERIOD_MS);

    /* Start the cyclic scheduler with 5ms frame timer */
    /* Note: scheduler_start_time will be initialized on first callback */
    add_repeating_timer_ms(-MINOR_FRAME_MS, frame_callback, NULL, &frame_timer);

    /* Main loop - scheduler runs in timer callback */
    while (true) {
        tight_loop_contents();  /* Idle loop */
    }

    return 0;
}
/*-----------------------------------------------------------*/
