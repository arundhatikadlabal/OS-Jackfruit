/*
 * cpu_hog.c - CPU-bound workload for scheduling experiments
 * Runs a tight arithmetic loop for a configurable duration.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char *argv[])
{
    int duration = argc > 1 ? atoi(argv[1]) : 10;
    time_t start = time(NULL);
    unsigned long long accumulator = 0;
    int elapsed = 0;

    while (1) {
        /* Tight CPU loop */
        for (int i = 0; i < 100000000; i++)
            accumulator += i * 3 + 7;

        elapsed = (int)(time(NULL) - start);
        printf("cpu_hog alive elapsed=%d accumulator=%llu\n",
               elapsed, accumulator);
        fflush(stdout);

        if (elapsed >= duration)
            break;
    }

    printf("cpu_hog done duration=%d accumulator=%llu\n",
           duration, accumulator);
    return 0;
}
