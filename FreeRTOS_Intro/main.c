#include <stdio.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "bsp.h"
#include "workload.h"

/*************************************************************/

/* Hyperperiod in milliseconds: LCM(5, 10, 20, 25, 50) = 100ms */
#define HYPERPERIOD_MS 100
#define MAX_LOGS_PER_HYPERPERIOD 50  /* Buffer size for logs */

/* Log entry for task execution */
typedef struct {
    const char* task_name;
    uint64_t release_time;   /* When task was released (theoretical) */
    uint64_t start_time;     /* When job started executing */
    uint64_t finish_time;    /* When job completed */
    uint64_t exec_time;      /* Execution duration */
} log_entry_t;

/* Task parameters structure */
typedef struct {
    const char* name;                  /* Task name */
    void (*job_func)(jobReturn_t*);   /* Workload function */
    uint32_t period_ms;                /* Period in milliseconds */
    uint32_t deadline_ms;              /* Deadline in milliseconds (implicit: D=T) */
    UBaseType_t priority;              /* Task priority */
    uint32_t job_count;                /* Job counter for release time calculation */
} task_params_t;

/* Task handles */
TaskHandle_t task_A_handle;
TaskHandle_t task_B_handle;
TaskHandle_t task_C_handle;
TaskHandle_t task_D_handle;
TaskHandle_t task_E_handle;
TaskHandle_t task_F_handle;

/* Global log buffer */
log_entry_t log_buffer[MAX_LOGS_PER_HYPERPERIOD];
volatile uint32_t log_count = 0;
SemaphoreHandle_t log_mutex;

/**
 * @brief Periodic task template
 *
 * This function serves as a template for all periodic tasks.
 * It implements the periodic execution pattern using vTaskDelayUntil.
 *
 * @param args Pointer to task_params_t structure
 */
void periodic_task(void *args);

/**
 * @brief Monitor task that prints statistics every hyperperiod
 *
 * @param args Unused
 */
void monitor_task(void *args);

/*************************************************************/

/**
 * @brief Main function.
 *
 * @return int
 */
int main()
{
    BSP_Init();  /* Initialize all components on the lab-kit. */

    printf("\n========================================\n");
    printf("FreeRTOS Periodic Task Scheduler\n");
    printf("========================================\n");
    printf("Task Periods and Priorities:\n");
    printf("  Task_B: 5ms   (Priority 6 - Highest)\n");
    printf("  Task_A: 10ms  (Priority 5)\n");
    printf("  Task_F: 20ms  (Priority 4)\n");
    printf("  Task_C: 25ms  (Priority 3)\n");
    printf("  Task_D: 50ms  (Priority 2)\n");
    printf("  Task_E: 50ms  (Priority 1)\n");
    printf("Priority Assignment: Rate Monotonic\n");
    printf("Hyperperiod: %d ms\n", HYPERPERIOD_MS);
    printf("========================================\n\n");

    /* Create mutex for log buffer protection */
    log_mutex = xSemaphoreCreateMutex();

    /* Define task parameters (static allocation for persistence) */
    static task_params_t params_A = {
        .name = "Task_A",
        .job_func = job_A,
        .period_ms = 10,
        .deadline_ms = 10,
        .priority = 5,
        .job_count = 0
    };

    static task_params_t params_B = {
        .name = "Task_B",
        .job_func = job_B,
        .period_ms = 5,
        .deadline_ms = 5,
        .priority = 6,  /* Highest priority (shortest period) */
        .job_count = 0
    };

    static task_params_t params_C = {
        .name = "Task_C",
        .job_func = job_C,
        .period_ms = 25,
        .deadline_ms = 25,
        .priority = 3,
        .job_count = 0
    };

    static task_params_t params_D = {
        .name = "Task_D",
        .job_func = job_D,
        .period_ms = 50,
        .deadline_ms = 50,
        .priority = 2,
        .job_count = 0
    };

    static task_params_t params_E = {
        .name = "Task_E",
        .job_func = job_E,
        .period_ms = 50,
        .deadline_ms = 50,
        .priority = 1,  /* Lowest priority */
        .job_count = 0
    };

    static task_params_t params_F = {
        .name = "Task_F",
        .job_func = job_F,
        .period_ms = 20,
        .deadline_ms = 20,
        .priority = 4,
        .job_count = 0
    };

    /* Create all periodic tasks */
    xTaskCreate(periodic_task, "Task_A", 512, &params_A, params_A.priority, &task_A_handle);
    xTaskCreate(periodic_task, "Task_B", 512, &params_B, params_B.priority, &task_B_handle);
    xTaskCreate(periodic_task, "Task_C", 512, &params_C, params_C.priority, &task_C_handle);
    xTaskCreate(periodic_task, "Task_D", 512, &params_D, params_D.priority, &task_D_handle);
    xTaskCreate(periodic_task, "Task_E", 512, &params_E, params_E.priority, &task_E_handle);
    xTaskCreate(periodic_task, "Task_F", 512, &params_F, params_F.priority, &task_F_handle);

    /* Create monitor task with lowest priority (0) */
    xTaskCreate(monitor_task, "Monitor", 512, NULL, 0, NULL);

    /* Start the FreeRTOS scheduler */
    vTaskStartScheduler();

    /* Should never reach here */
    while (true) {
        sleep_ms(1000);
    }

    return 0;
}
/*-----------------------------------------------------------*/

/**
 * @brief Periodic task template implementation
 *
 * This function implements the periodic task pattern:
 * 1. Initialize timing variables
 * 2. Infinite loop:
 *    - Calculate release time
 *    - Execute the job function
 *    - Log execution information
 *    - Wait until next period using vTaskDelayUntil
 *
 * @param args Pointer to task_params_t structure
 */
void periodic_task(void *args)
{
    task_params_t *params = (task_params_t *)args;
    jobReturn_t result;
    TickType_t xLastWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS(params->period_ms);

    /* Initialize the xLastWakeTime variable with the current time */
    xLastWakeTime = xTaskGetTickCount();

    /* Periodic task loop */
    for (;;) {
        /* Calculate theoretical release time for this job */
        uint64_t release_time_us = (uint64_t)params->job_count * params->period_ms * 1000;

        /* Execute the job */
        params->job_func(&result);

        /* Log execution information */
        if (xSemaphoreTake(log_mutex, portMAX_DELAY) == pdTRUE) {
            if (log_count < MAX_LOGS_PER_HYPERPERIOD) {
                log_buffer[log_count].task_name = params->name;
                log_buffer[log_count].release_time = release_time_us;
                log_buffer[log_count].start_time = result.start;
                log_buffer[log_count].finish_time = result.stop;
                log_buffer[log_count].exec_time = result.stop - result.start;
                log_count++;
            }
            xSemaphoreGive(log_mutex);
        }

        /* Increment job counter */
        params->job_count++;

        /* Wait for the next period */
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}
/*-----------------------------------------------------------*/

/**
 * @brief Monitor task implementation
 *
 * This task runs with the lowest priority and prints all execution logs
 * every hyperperiod (100ms).
 */
void monitor_task(void *args)
{
    (void)args;
    TickType_t xLastWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS(HYPERPERIOD_MS);
    uint32_t hyperperiod_count = 0;

    /* Initialize the xLastWakeTime variable with the current time */
    xLastWakeTime = xTaskGetTickCount();

    /* Wait one hyperperiod before first print to collect data */
    vTaskDelayUntil(&xLastWakeTime, xPeriod);

    for (;;) {
        hyperperiod_count++;

        printf("\n========== Hyperperiod %u ==========\n", hyperperiod_count);

        /* Get exclusive access to log buffer */
        if (xSemaphoreTake(log_mutex, portMAX_DELAY) == pdTRUE) {
            /* Print all logged executions */
            for (uint32_t i = 0; i < log_count; i++) {
                printf("[%s] R:%llu S:%llu C:%llu E:%llu us\n",
                       log_buffer[i].task_name,
                       log_buffer[i].release_time,
                       log_buffer[i].start_time,
                       log_buffer[i].finish_time,
                       log_buffer[i].exec_time);
            }
            printf("Total logs: %u\n", log_count);
            printf("====================================\n\n");

            /* Reset log buffer for next hyperperiod */
            log_count = 0;

            xSemaphoreGive(log_mutex);
        }

        /* Wait for next hyperperiod */
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}
/*-----------------------------------------------------------*/
