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
} job_record_t;

/* Global variables */
static uint32_t current_frame = 0;
static repeating_timer_t frame_timer;
static job_record_t job_log[MAX_JOBS_PER_HYPERPERIOD];
static uint32_t job_count = 0;
static uint32_t hyperperiod_count = 0;

/* Static schedule table for the hyperperiod (20 frames)
 * Optimized cyclic schedule meeting all deadlines within 5ms frames
 * Execution pattern per hyperperiod:
 *   A: frames 1,3,5,7,9,11,13,15,17,19 (10 times, period=2 frames)
 *   B: frames 0-19 (20 times, period=1 frame) - ALWAYS FIRST
 *   C: frames 2,7,12,17 (4 times, period=5 frames)
 *   D: frames 2,12 (2 times, period=10 frames)
 *   E: frames 0,10 (2 times, period=10 frames)
 *   F: frames 4,8,12,16 (4 times, period=4 frames) + frame 0 of next hyperperiod
 *
 * Note: Task_F executes 4 times in frames 4,8,12,16, with 5th instance at start of next hyperperiod
 */
static frame_schedule_t schedule[NUM_FRAMES] = {
    /* Frame 0 (0ms): Task_B, Task_E | Load: 1+4=5ms */
    { .tasks = {job_B, job_E}, .names = {"Task_B", "Task_E"}, .num_tasks = 2 },
    /* Frame 1 (5ms): Task_B, Task_A | Load: 1+1=2ms */
    { .tasks = {job_B, job_A}, .names = {"Task_B", "Task_A"}, .num_tasks = 2 },
    /* Frame 2 (10ms): Task_B, Task_D, Task_C | Load: 1+2+2=5ms */
    { .tasks = {job_B, job_D, job_C}, .names = {"Task_B", "Task_D", "Task_C"}, .num_tasks = 3 },
    /* Frame 3 (15ms): Task_B, Task_A | Load: 1+1=2ms */
    { .tasks = {job_B, job_A}, .names = {"Task_B", "Task_A"}, .num_tasks = 2 },
    /* Frame 4 (20ms): Task_B, Task_F | Load: 1+2=3ms */
    { .tasks = {job_B, job_F}, .names = {"Task_B", "Task_F"}, .num_tasks = 2 },
    /* Frame 5 (25ms): Task_B, Task_A | Load: 1+1=2ms */
    { .tasks = {job_B, job_A}, .names = {"Task_B", "Task_A"}, .num_tasks = 2 },
    /* Frame 6 (30ms): Task_B | Load: 1ms */
    { .tasks = {job_B}, .names = {"Task_B"}, .num_tasks = 1 },
    /* Frame 7 (35ms): Task_B, Task_A, Task_C | Load: 1+1+2=4ms */
    { .tasks = {job_B, job_A, job_C}, .names = {"Task_B", "Task_A", "Task_C"}, .num_tasks = 3 },
    /* Frame 8 (40ms): Task_B, Task_F | Load: 1+2=3ms */
    { .tasks = {job_B, job_F}, .names = {"Task_B", "Task_F"}, .num_tasks = 2 },
    /* Frame 9 (45ms): Task_B, Task_A | Load: 1+1=2ms */
    { .tasks = {job_B, job_A}, .names = {"Task_B", "Task_A"}, .num_tasks = 2 },
    /* Frame 10 (50ms): Task_B, Task_E | Load: 1+4=5ms */
    { .tasks = {job_B, job_E}, .names = {"Task_B", "Task_E"}, .num_tasks = 2 },
    /* Frame 11 (55ms): Task_B, Task_A | Load: 1+1=2ms */
    { .tasks = {job_B, job_A}, .names = {"Task_B", "Task_A"}, .num_tasks = 2 },
    /* Frame 12 (60ms): Task_B, Task_D, Task_C, Task_F | Load: 1+2+2+2=7ms - CAUTION: Overload by 2ms! */
    { .tasks = {job_B, job_D, job_C, job_F}, .names = {"Task_B", "Task_D", "Task_C", "Task_F"}, .num_tasks = 4 },
    /* Frame 13 (65ms): Task_B, Task_A | Load: 1+1=2ms */
    { .tasks = {job_B, job_A}, .names = {"Task_B", "Task_A"}, .num_tasks = 2 },
    /* Frame 14 (70ms): Task_B | Load: 1ms */
    { .tasks = {job_B}, .names = {"Task_B"}, .num_tasks = 1 },
    /* Frame 15 (75ms): Task_B, Task_A | Load: 1+1=2ms */
    { .tasks = {job_B, job_A}, .names = {"Task_B", "Task_A"}, .num_tasks = 2 },
    /* Frame 16 (80ms): Task_B, Task_F | Load: 1+2=3ms */
    { .tasks = {job_B, job_F}, .names = {"Task_B", "Task_F"}, .num_tasks = 2 },
    /* Frame 17 (85ms): Task_B, Task_A, Task_C | Load: 1+1+2=4ms */
    { .tasks = {job_B, job_A, job_C}, .names = {"Task_B", "Task_A", "Task_C"}, .num_tasks = 3 },
    /* Frame 18 (90ms): Task_B | Load: 1ms */
    { .tasks = {job_B}, .names = {"Task_B"}, .num_tasks = 1 },
    /* Frame 19 (95ms): Task_B, Task_A | Load: 1+1=2ms */
    { .tasks = {job_B, job_A}, .names = {"Task_B", "Task_A"}, .num_tasks = 2 }
};


/**
 * @brief Print all job executions from the last hyperperiod
 */
void print_hyperperiod_report(void) {
    printf("\n========== Hyperperiod %u Report ==========\n", hyperperiod_count);
    printf("Frame | Task   | Release    | Start      | Complete   | Exec Time\n");
    printf("------+--------+------------+------------+------------+-----------\n");

    for (uint32_t i = 0; i < job_count; i++) {
        printf(" %2u   | %-6s | %10llu | %10llu | %10llu | %6llu us\n",
               job_log[i].frame,
               job_log[i].task_name,
               job_log[i].release_time,
               job_log[i].start_time,
               job_log[i].completion_time,
               job_log[i].exec_time);
    }

    printf("==============================================\n");
    printf("Total jobs executed: %u\n\n", job_count);
}

/**
 * @brief Frame timer callback - executes tasks for the current frame
 *
 * @param tmr Pointer to the repeating timer
 * @return true to keep timer running
 */
bool frame_callback(repeating_timer_t *tmr) {
    jobReturn_t result;
    uint64_t frame_start = time_us_64();
    uint32_t local_frame = current_frame % NUM_FRAMES;

    /* Execute all tasks scheduled for this frame (in order) */
    for (uint8_t i = 0; i < schedule[local_frame].num_tasks; i++) {
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
            job_count++;
        }
    }

    uint64_t frame_end = time_us_64();
    uint64_t frame_duration = frame_end - frame_start;

    /* Check for frame overrun */
    if (frame_duration > (MINOR_FRAME_MS * 1000)) {
        BSP_ToggleLED(LED_RED);
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
    add_repeating_timer_ms(-MINOR_FRAME_MS, frame_callback, NULL, &frame_timer);

    /* Main loop - scheduler runs in timer callback */
    while (true) {
        tight_loop_contents();  /* Idle loop */
    }

    return 0;
}
/*-----------------------------------------------------------*/
