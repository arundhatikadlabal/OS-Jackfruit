/*
 * memory_hog.c - Memory-consuming workload for kernel monitor testing
 * Allocates increasing chunks of memory until killed or finished.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHUNK_SIZE (1024 * 1024) /* 1 MB per step */

int main(int argc, char *argv[])
{
    int max_mb = argc > 1 ? atoi(argv[1]) : 16;
    int delay_s = argc > 2 ? atoi(argv[2]) : 1;

    printf("memory_hog: will allocate up to %d MB, %ds between steps\n",
           max_mb, delay_s);
    fflush(stdout);

    for (int i = 1; i <= max_mb; i++) {
        char *p = malloc(CHUNK_SIZE);
        if (!p) {
            fprintf(stderr, "memory_hog: malloc failed at %d MB\n", i);
            break;
        }
        /* Touch every page so it is actually resident */
        memset(p, i & 0xFF, CHUNK_SIZE);

        printf("memory_hog: allocated %d MB total\n", i);
        fflush(stdout);
        sleep(delay_s);
    }

    printf("memory_hog: done. Sleeping indefinitely.\n");
    fflush(stdout);
    while (1)
        sleep(60);

    return 0;
}
