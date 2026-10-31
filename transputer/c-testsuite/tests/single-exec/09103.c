int printf(const char *, ...);
void __move2dinit(int num_rows, int dst_stride, int src_stride);
void __move2dall(int row_bytes, void *dst, const void *src);
/* 6x4 source inside an 8-wide array; copy the 6x4 block into a 10-wide dst. */
static unsigned char src[4 * 8];
static unsigned char dst[4 * 10];
int main(void) {
    int r, c, bad = 0;
    for (r = 0; r < 4; r++) for (c = 0; c < 8; c++) src[r * 8 + c] = (unsigned char)(r * 16 + c + 1);
    for (r = 0; r < 4 * 10; r++) dst[r] = 0xEE;
    __move2dinit(4, 10, 8);            /* 4 rows, dst stride 10, src stride 8 */
    __move2dall(6, dst, src);          /* 6 bytes per row */
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 10; c++) {
            unsigned char want = (c < 6) ? (unsigned char)(r * 16 + c + 1) : 0xEE;
            if (dst[r * 10 + c] != want) bad++;
        }
    }
    printf("move2d bad=%d dst00=%d dst05=%d dst06=%d dst30=%d\n",
           bad, dst[0], dst[5], dst[6], dst[30]);
    return 0;
}
