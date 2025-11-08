#ifndef WORKLOAD_H
#define WORKLOAD_H

#define CYCLES_PER_MS  150000
#define CYCLES_PER_US  (CYCLES_PER_MS / 1000)

#define EXECUTION_TIME_A ((1 * CYCLES_PER_MS) - (CYCLES_PER_US * 10))
#define EXECUTION_TIME_B ((1 * CYCLES_PER_MS) - (CYCLES_PER_US * 10))
#define EXECUTION_TIME_C ((2 * CYCLES_PER_MS) - (CYCLES_PER_US * 10))
#define EXECUTION_TIME_D ((2 * CYCLES_PER_MS) - (CYCLES_PER_US * 10))
#define EXECUTION_TIME_E ((4 * CYCLES_PER_MS) - (CYCLES_PER_US * 10))
#define EXECUTION_TIME_F ((2 * CYCLES_PER_MS) - (CYCLES_PER_US * 10))

typedef struct {
    uint64_t start;
    uint64_t stop;
} jobReturn_t;

void job_A(jobReturn_t* retval);
void job_B(jobReturn_t* retval);
void job_C(jobReturn_t* retval);
void job_D(jobReturn_t* retval);
void job_E(jobReturn_t* retval);
void job_F(jobReturn_t* retval);

#endif /* WORKLOAD_H */
