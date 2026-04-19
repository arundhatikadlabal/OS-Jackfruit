/*
 * io_pulse.c - I/O-bound workload for scheduling experiments
 * Repeatedly writes and reads a temp file to generate I/O pressure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BUF_SIZE  (64 * 1024)   /* 64 KB */
#define ITERS     200

int main(int argc, char *argv[])
{
    int duration = argc > 1 ? atoi(argv[1]) : 10;
    time_t start = time(NULL);
    char buf[BUF_SIZE];
    memset(buf, 'A', sizeof(buf));
    int cycle = 0;

    while ((int)(time(NULL) - start) < duration) {
        FILE *f = tmpfile();
        if (!f) {
            perror("tmpfile");
            return 1;
        }
        /* Write */
        for (int i = 0; i < ITERS; i++)
            fwrite(buf, 1, BUF_SIZE, f);

        /* Read back */
        rewind(f);
        size_t total = 0;
        size_t n;
        while ((n = fread(buf, 1, BUF_SIZE, f)) > 0)
            total += n;

        fclose(f);
        cycle++;
        printf("io_pulse cycle=%d bytes_read=%zu elapsed=%ds\n",
               cycle, total, (int)(time(NULL) - start));
        fflush(stdout);
    }

    printf("io_pulse done cycles=%d\n", cycle);
    return 0;
}
