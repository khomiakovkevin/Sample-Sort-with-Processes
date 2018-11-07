#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>

#include "float_vec.h"
#include "barrier.h"
#include "utils.h"

int
randomize(long max_value)
{
    int rand = random(); 
    return (int)((float)rand / RAND_MAX * max_value);
}

int
comp_floats(const void* i, const void* j)
{
	float new_i = *((float*) i);
        float new_j = *((float*) j);

    if (new_i < new_j)
    {
        return -1;
    }
    else if (new_i > new_j)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

void
qsort_floats(floats* xs)
{
    qsort(xs->data, xs->size, sizeof(float), comp_floats);
}

floats*
sample(float* data, long size, int P)
{
    int counter = 3 * (P - 1);
    float samps[counter];
    floats* floats = make_floats(0);

    for (int ii = 0; ii < counter; ii++) 
    {
        samps[ii] = data[randomize(size)];
    }

    qsort(&samps, counter, sizeof(float), comp_floats);
    floats_push(floats, 0.0f);

    for (int ii = 0; ii < counter; ii += 3) 
    {
        floats_push(floats, samps[ii + 1]);
    }

    floats_push(floats, INFINITY);
    return floats;
}

void
sort_worker(int pnum, float* data, long size, int P, floats* samps, long* sizes, barrier* bb)
{
    float s_d = samps->data[pnum];
    float s_d_inc = samps->data[pnum + 1];

    int begin = 0;
    int end = 0;
    int j = 0;

    floats* floats = make_floats(0);

    for (int ii = 0; ii < size; ii++) 
    {
        if (data[ii] >= s_d && data[ii] < s_d_inc)
        {
            floats_push(floats, data[ii]);
        }
    }
    sizes[pnum] = floats->size;

    printf("%d: start %.04f, count %ld\n", pnum, s_d, sizes[pnum]);

    qsort_floats(floats);
    barrier_wait(bb);

    for (int ii = 0; ii < pnum; ii++)
    {
        begin = begin + sizes[ii];
    }

    for (int ii = 0; ii <= pnum; ii++)
    {
        end = end + sizes[ii];
    }

    for (int ii = begin; ii < end; ii++)
    {
        data[ii] = floats->data[j];
        j++;
    }

    free_floats(floats);
}

void
run_sort_workers(float* data, long size, int P, floats* samps, long* sizes, barrier* bb)
{
    pid_t kids[P];
    (void) kids;

    for (int ii = 0; ii < P; ++ii) {
        if(!(kids[ii] = fork()))
        {
            sort_worker(ii, data, size, P, samps, sizes, bb);
            exit(0);
        }
    }

    for (int ii = 0; ii < P; ++ii) {
        int rv = waitpid(kids[ii], 0, 0);
	check_rv(rv);
    }
}

void
sample_sort(float* data, long size, int P, long* sizes, barrier* bb)
{
    floats* samps = sample(data, size, P);
    run_sort_workers(data, size, P, samps, sizes, bb);
    free_floats(samps);
}

int
main(int argc, char* argv[])
{
    alarm(120);
    
    if (argc != 3) {
        printf("Usage:\n");
        printf("\t%s P data.dat\n", argv[0]);
        return 1;
    }

    const int P = atoi(argv[1]);
    const char* fname = argv[2];

    seed_rng();

    int rv;
    struct stat st;
    rv = stat(fname, &st);
    check_rv(rv);

    const int fsize = st.st_size;
    if (fsize < 8) {
        printf("File too small.\n");
        return 1;
    }

    int fd = open(fname, O_RDWR);
    check_rv(fd);

    void* file = mmap(0, fsize, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

    (void) file; 

    long count = *(long*)file;

    float *data = (float*)file + sizeof(long) / sizeof(float);

    long sizes_bytes = P * sizeof(long);
    long* sizes = mmap(0, sizes_bytes, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);

    barrier* bb = make_barrier(P);

    sample_sort(data, count, P, sizes, bb);

    free_barrier(bb);

    munmap(file, fsize);
    munmap(sizes, sizes_bytes);

    return 0;
}

