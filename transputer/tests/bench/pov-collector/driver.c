/* Feed captured worker packets to the real muxdemux process over channels. */
#include <conc.h>
#include <stdio.h>
#include <stdlib.h>

enum { channels = 3, rows = 480, packet_size = 9 + 640 * 4 };
static int channel_count = channels;
static Channel requests[channels], replies[channels];
static Channel *input[channels], *output[channels];
static unsigned int feeder_workspace[2048];
static unsigned char packets[rows][1 + packet_size];
static unsigned int started;

int collector_main(void);

void *get_param(int index)
{
    switch (index) {
    case 1: return LINK0IN;
    case 2: return LINK0OUT;
    case 3: return input;
    case 4: return &channel_count;
    case 5: return output;
    case 6: return &channel_count;
    }
    abort();
}

void collector_start(void)
{
    started = __ldtimer();
}

void collector_stop(void)
{
    unsigned int elapsed = __ldtimer() - started;
    printf("COLLECTOR elapsed %u\n", elapsed);
    if (fflush(stdout))
        abort();
}

static void send_packets(void)
{
    static const unsigned char quit[] = { 35, 0, 0, 0, 0, 0 };
    int i, slot, length = packet_size;
    for (i = 0; i < rows; i++) {
        slot = packets[i][0];
        ChanOut(input[slot], &length, 2);
        ChanOut(input[slot], packets[i] + 1, length);
    }
    length = sizeof quit;
    for (i = 0; i < channels; i++) {
        int response = 0;
        unsigned char status;
        ChanOut(input[i], &length, 2);
        ChanOut(input[i], quit, length);
        ChanIn(output[i], &response, 2);
        if (response != 1)
            abort();
        ChanIn(output[i], &status, 1);
        if (status != 0)
            abort();
    }
}

int main(int argc, char **argv)
{
    FILE *file = fopen("packets.bin", "rb");
    int i;
    (void)argc;
    (void)argv;
    if (!file || fread(packets, 1, sizeof packets, file) != sizeof packets ||
        fgetc(file) != EOF || ferror(file) || fclose(file))
        return 1;
    for (i = 0; i < rows; i++)
        if (packets[i][0] >= channels)
            return 1;

    for (i = 0; i < channels; i++) {
        ChanInit(&requests[i]);
        ChanInit(&replies[i]);
        input[i] = &requests[i];
        output[i] = &replies[i];
    }
    /* The feeder touches only soft channels after startup. The collector
     * is the sole user of the runtime's host I/O packet buffer and MOVE2D. */
    PRun(PSetup(feeder_workspace, sizeof feeder_workspace, send_packets));
    return collector_main();
}
