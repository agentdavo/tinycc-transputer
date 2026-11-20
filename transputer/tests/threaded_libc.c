/* Protected bFLT malloc and stdio regression for the opt-in uClibc build. */
#include <bits/uClibc_mutex.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WORKERS 3
#define ROUNDS 128
#define SLOTS 8
#define RECORD_BYTES 10

extern long __transputer_syscall(long, long, long, long, long, long, long);
extern struct __uclibc_tp_lock __malloc_heap_lock;

struct worker {
    int id, error;
    volatile int at_malloc, after_malloc, at_stdio;
    unsigned char *handoff;
};

struct chunk {
    unsigned char *data;
    unsigned size;
    unsigned char value;
};

static FILE *shared;
static struct worker workers[WORKERS];
static const char *const scratch_paths[] = {
    "/tmp/tp-stdio-0", "/tmp/tp-stdio-1", "/tmp/tp-stdio-2"
};

static void yield(void)
{
    __transputer_syscall(124, 0, 0, 0, 0, 0, 0);
}

static int check_bytes(const unsigned char *data, unsigned size, unsigned char value)
{
    unsigned i;
    for (i = 0; i < size; i++)
        if (data[i] != value)
            return 0;
    return 1;
}

static int exercise_heap(struct chunk *chunk, int id, int round)
{
    unsigned size = 41 + ((round * 571 + id * 2971) & 8191);
    unsigned char *p;
    if (chunk->data) {
        unsigned prefix = size < chunk->size ? size : chunk->size;
        if (!check_bytes(chunk->data, chunk->size, chunk->value))
            return 0;
        p = realloc(chunk->data, size);
        if (!p)
            return 0;
        chunk->data = p;
        if (!check_bytes(p, prefix, chunk->value))
            return 0;
    } else {
        p = calloc(size, 1);
        if (!p)
            return 0;
        chunk->data = p;
        if (!check_bytes(p, size, 0))
            return 0;
    }
    chunk->size = size;
    chunk->value = id * 73 + round;
    memset(p, chunk->value, size);
    return 1;
}

static void record(char *out, int id, int round)
{
    int i;
    out[0] = '0' + id;
    out[1] = '0' + round / 100;
    out[2] = '0' + round / 10 % 10;
    out[3] = '0' + round % 10;
    for (i = 4; i < RECORD_BYTES - 1; i++)
        out[i] = 'a' + (id * 3 + round + i) % 26;
    out[RECORD_BYTES - 1] = '\n';
}

static int exercise_streams(int id, int round)
{
    char bytes[RECORD_BYTES], input[RECORD_BYTES + 1];
    FILE *stream;
    int ok;
    record(bytes, id, round);
    if (round % 8 == 0) {
        flockfile(shared);
        flockfile(shared);
        ok = fputc(bytes[0], shared) != EOF;
        yield();
        if (fwrite(bytes + 1, 1, sizeof(bytes) - 1, shared) != sizeof(bytes) - 1)
            ok = 0;
        funlockfile(shared);
        funlockfile(shared);
    } else {
        ok = fwrite(bytes, 1, sizeof(bytes), shared) == sizeof(bytes);
    }
    if (!ok || round % 8)
        return ok;

    stream = fopen(scratch_paths[id], "w+");
    if (!stream)
        return 0;
    ok = fwrite(bytes, 1, sizeof(bytes), stream) == sizeof(bytes);
    yield();
    if (fflush(NULL) || fseek(stream, 0, SEEK_SET) ||
        !fgets(input, sizeof(input), stream) || memcmp(input, bytes, sizeof(bytes)))
        ok = 0;
    if (fclose(stream))
        ok = 0;
    unlink(scratch_paths[id]);
    return ok;
}

static void *run_worker(void *argument)
{
    struct worker *worker = argument;
    struct chunk slots[SLOTS];
    void *result;
    int round, i;

    memset(slots, 0, sizeof(slots));
    worker->at_malloc = 1;
    result = malloc(64 + worker->id);
    worker->after_malloc = 1;
    if (!result || !check_bytes(worker->handoff, 64, 190 + worker->id))
        worker->error = 1;
    free(worker->handoff);
    if (!ftrylockfile(shared)) {
        funlockfile(shared);
        worker->error = 2;
    }
    worker->at_stdio = 1;
    for (round = 0; round < ROUNDS && !worker->error; round++) {
        if (!exercise_heap(&slots[round % SLOTS], worker->id, round))
            worker->error = 3;
        else if (!exercise_streams(worker->id, round))
            worker->error = 4;
        yield();
    }
    for (i = 0; i < SLOTS; i++) {
        if (slots[i].data && !check_bytes(slots[i].data, slots[i].size, slots[i].value))
            worker->error = 5;
        free(slots[i].data);
    }
    return result;
}

static int verify_records(void)
{
    char expected[RECORD_BYTES];
    char *bytes = malloc(RECORD_BYTES);
    int next[WORKERS] = { 0 }, i, id, ok;
    if (!bytes)
        return 0;
    ok = fseek(shared, 0, SEEK_SET) == 0;
    for (i = 0; i < WORKERS * ROUNDS && ok; i++) {
        if (fread(bytes, 1, RECORD_BYTES, shared) != RECORD_BYTES) {
            ok = 0;
            break;
        }
        id = bytes[0] - '0';
        if (id < 0 || id >= WORKERS || next[id] == ROUNDS) {
            ok = 0;
            break;
        }
        record(expected, id, next[id]++);
        ok = memcmp(bytes, expected, RECORD_BYTES) == 0;
    }
    if (ok)
        ok = getc(shared) == EOF && feof(shared) && !ferror(shared);
    free(bytes);
    return ok;
}

int test_threaded_libc(void)
{
    pthread_t threads[WORKERS];
    int i, count = 0, ok;

    shared = fopen("/tmp/tp-stdio-shared", "w+");
    if (!shared)
        return 1;
    flockfile(stdout);
    fputs("LIBC_THREADS_TEST: start\n", stdout);
    fflush(stdout);
    funlockfile(stdout);
    flockfile(shared);
    __uclibc_tp_lock(&__malloc_heap_lock);
    for (i = 0; i < WORKERS; i++) {
        workers[i].id = i;
        workers[i].handoff = malloc(64);
        if (!workers[i].handoff)
            break;
        memset(workers[i].handoff, 190 + i, 64);
        if (pthread_create(&threads[i], 0, run_worker, &workers[i])) {
            free(workers[i].handoff);
            break;
        }
        count++;
    }
    ok = count == WORKERS;
    for (i = 0; i < count; i++) {
        while (!workers[i].at_malloc)
            yield();
        if (workers[i].after_malloc)
            ok = 0;
    }
    __uclibc_tp_unlock(&__malloc_heap_lock);
    for (i = 0; i < count; i++)
        while (!workers[i].at_stdio)
            yield();
    funlockfile(shared);
    for (i = 0; i < count; i++) {
        void *result = 0;
        if (pthread_join(threads[i], &result) || !result || workers[i].error)
            ok = 0;
        free(result);
    }
    if (ok)
        ok = verify_records();
    if (fclose(shared))
        ok = 0;
    unlink("/tmp/tp-stdio-shared");
    return ok ? 0 : 2;
}
