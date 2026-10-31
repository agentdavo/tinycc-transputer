int printf(const char *, ...);
/* Mirror struct kiocb: 32-bit pointer, then loff_t at offset 4, then flags. */
struct kio { void *filp; long long pos; unsigned int flags; int weight; };
static struct kio io;
static int femu_read(struct kio *iocb, unsigned long count) {
    if (iocb->pos < 0)
        return -22;
    if (iocb->pos >= 0x7fffffffffffLL)
        return 0;
    return (int)count;
}
int main(void) {
    int r1, r2, r3, r4;
    io.filp = (void *)&io; io.flags = 0; io.weight = 7;
    io.pos = 0;              r1 = femu_read(&io, 256);
    io.pos = -1;             r2 = femu_read(&io, 256);
    io.pos = 4096;           r3 = femu_read(&io, 256);
    io.pos = 0x7fffffffffffLL; r4 = femu_read(&io, 256);
    printf("r1=%d r2=%d r3=%d r4=%d\n", r1, r2, r3, r4);
    return 0;
}
