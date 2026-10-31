/*
 * transputer-mksd - build a T9000-style SD boot image for the t4 emulator
 *
 * Layout written into the (128MB) SD card image:
 *
 *   card 0x00000100   XiP boot stub (transputer code, runs from the
 *                     boot bank window at guest 0x7F000000)
 *   card 0x00FFFFF8   BootIptr  -> 0x7F000100          (TIS 10.5.2)
 *   card 0x00FFFFFC   BootWptr  -> RAM scratch above the payload
 *   card 0x00001000   optional true-XiP kernel text (RX PT_LOAD)
 *   card 0x01000000   flat kernel payload, or the RAM PT_LOAD for a
 *                     split XiP image, visible to the stub at guest
 *                     0x21000000 through the flat SD window
 *   card 0x04000000   64MB Linux/ext2 partition (MBR partition 1)
 *
 * The stub executes in place from the boot bank and does exactly one
 * job: `move` the payload from the SD window to its load address in
 * RAM (one instruction — the emulator implements move as memcpy) and
 * gcall the entry point.  The rest of the card is left untouched, so
 * a persistent T4_SD_IMAGE keeps its filesystem contents.
 *
 * Usage: transputer-mksd [--entry HEX] [--rootfs rootfs.ext2]
 *                        <vmlinux> <sd-image>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define CARD_SIZE      (128u * 1024 * 1024)
#define BANK_SIZE      (16u * 1024 * 1024)
#define BANK_GUEST     0x7F000000u
#define STUB_CARD_OFF  0x100u
#define STUB_XIP_ADDR  0x7F000100u
#define PAYLOAD_OFF    0x01000000u          /* first byte after boot bank */
#define PAYLOAD_GUEST  0x21000000u          /* via flat SD window        */
#define ROOTFS_OFF     0x04000000u          /* stable 64MiB partition     */
#define ROOTFS_SIZE    (CARD_SIZE - ROOTFS_OFF)
#define VEC_IPTR_OFF   0x00FFFFF8u
#define VEC_WPTR_OFF   0x00FFFFFCu
#define MBR_PART_OFF   0x1BEu

static unsigned char stub[128];
static int stublen;

static void emit(unsigned char b) { stub[stublen++] = b; }

/* Classic INMOS prefixing: build OReg for direct function `fn`
 * (fn = opcode << 4) with operand o, using pfix (0x20) / nfix (0x60). */
static void prefix(int32_t o, unsigned char fn)
{
    if (o >= 0 && o < 16) {
        emit((unsigned char)(fn | (o & 0xF)));
    } else if (o < 0) {
        prefix((int32_t)(~o) >> 4, 0x60);
        emit((unsigned char)(fn | (o & 0xF)));
    } else {
        prefix(o >> 4, 0x20);
        emit((unsigned char)(fn | (o & 0xF)));
    }
}

static void ldc(uint32_t v)  { prefix((int32_t)v, 0x40); }
static void opr(uint32_t op) { prefix((int32_t)op, 0xF0); }

static void put_le32(FILE *f, long off, uint32_t v)
{
    unsigned char b[4] = { (unsigned char)(v & 0xFF),
                           (unsigned char)((v >> 8) & 0xFF),
                           (unsigned char)((v >> 16) & 0xFF),
                           (unsigned char)((v >> 24) & 0xFF) };
    fseek(f, off, SEEK_SET);
    fwrite(b, 1, 4, f);
}

static void le32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static int copy_file(FILE *dst, long dst_off, const char *path,
                     uint32_t limit)
{
    FILE *src;
    unsigned char buf[64 * 1024];
    long size;
    uint32_t done = 0;

    src = fopen(path, "rb");
    if (!src) { perror(path); return -1; }
    if (fseek(src, 0, SEEK_END) != 0 || (size = ftell(src)) < 0) {
        perror(path);
        fclose(src);
        return -1;
    }
    if ((unsigned long)size > limit) {
        fprintf(stderr, "%s: %ld bytes exceeds partition size %u\n",
                path, size, limit);
        fclose(src);
        return -1;
    }
    rewind(src);
    fseek(dst, dst_off, SEEK_SET);
    while (done < (uint32_t)size) {
        size_t want = sizeof(buf);
        size_t got;
        if (want > (uint32_t)size - done)
            want = (uint32_t)size - done;
        got = fread(buf, 1, want, src);
        if (got != want || fwrite(buf, 1, got, dst) != got) {
            perror("rootfs copy");
            fclose(src);
            return -1;
        }
        done += (uint32_t)got;
    }
    fclose(src);
    return 0;
}

static void write_mbr(FILE *sd)
{
    unsigned char table[66];
    unsigned char *p = table;

    memset(table, 0, sizeof(table));
    p[0] = 0x00;                 /* not BIOS-bootable */
    p[1] = 0xFE; p[2] = 0xFF; p[3] = 0xFF; /* synthetic CHS */
    p[4] = 0x83;                 /* Linux filesystem */
    p[5] = 0xFE; p[6] = 0xFF; p[7] = 0xFF;
    le32(p + 8, ROOTFS_OFF / 512u);
    le32(p + 12, ROOTFS_SIZE / 512u);
    table[64] = 0x55;
    table[65] = 0xAA;
    fseek(sd, MBR_PART_OFF, SEEK_SET);
    fwrite(table, 1, sizeof(table), sd);
}

static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const unsigned char *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static unsigned char *read_segment(FILE *elf, uint32_t off, uint32_t size,
                                   const char *what)
{
    unsigned char *data = malloc(size ? size : 1);

    if (!data) { perror("malloc"); return NULL; }
    if (fseek(elf, off, SEEK_SET) != 0 || fread(data, 1, size, elf) != size) {
        fprintf(stderr, "short read of %s\n", what);
        free(data);
        return NULL;
    }
    return data;
}

int main(int argc, char **argv)
{
    FILE *elf, *sd;
    unsigned char eh[64], ph[2][32];
    uint32_t e_entry, e_phoff, p_offset, p_vaddr, p_filesz, p_memsz;
    uint32_t text_offset = 0, text_vaddr = 0, text_filesz = 0;
    uint32_t boot_wptr;
    unsigned char *payload;
    unsigned char *xip_text = NULL;
    const char *elf_path = NULL;
    const char *sd_path = NULL;
    const char *rootfs_path = NULL;
    const char *entry_arg = NULL;
    long n;
    int i;
    int xip = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--entry") && i + 1 < argc) {
            entry_arg = argv[++i];
        } else if (!strcmp(argv[i], "--rootfs") && i + 1 < argc) {
            rootfs_path = argv[++i];
        } else if (!elf_path) {
            elf_path = argv[i];
        } else if (!sd_path) {
            sd_path = argv[i];
        } else if (!entry_arg) {
            /* Backward-compatible third positional entry address. */
            entry_arg = argv[i];
        } else {
            fprintf(stderr, "%s: unexpected argument: %s\n", argv[0], argv[i]);
            return 1;
        }
    }
    if (!elf_path || !sd_path) {
        fprintf(stderr,
                "usage: %s [--entry HEX] [--rootfs rootfs.ext2]"
                " <vmlinux> <sd-image>\n", argv[0]);
        return 1;
    }

    elf = fopen(elf_path, "rb");
    if (!elf) { perror(elf_path); return 1; }
    if (fread(eh, 1, sizeof eh, elf) != sizeof eh ||
        memcmp(eh, "\177ELF", 4) != 0) {
        fprintf(stderr, "%s: not an ELF file\n", elf_path);
        return 1;
    }
    e_entry = rd32(eh + 0x18);
    e_phoff = rd32(eh + 0x1C);
    if (rd16(eh + 0x2A) != sizeof(ph[0]) || rd16(eh + 0x2C) < 1) {
        fprintf(stderr, "%s: unsupported program header table\n", elf_path);
        return 1;
    }
    fseek(elf, e_phoff, SEEK_SET);
    if (fread(ph[0], 1, sizeof ph[0], elf) != sizeof ph[0] ||
        rd32(ph[0]) != 1) {
        fprintf(stderr, "%s: first program header is not PT_LOAD\n", elf_path);
        return 1;
    }
    p_offset = rd32(ph[0] + 4);
    p_vaddr  = rd32(ph[0] + 8);
    p_filesz = rd32(ph[0] + 16);
    p_memsz  = rd32(ph[0] + 20);

    if (rd16(eh + 0x2C) >= 2 &&
        p_vaddr >= BANK_GUEST + 0x1000u && p_vaddr < 0x80000000u) {
        if (fread(ph[1], 1, sizeof ph[1], elf) != sizeof ph[1] ||
            rd32(ph[1]) != 1) {
            fprintf(stderr, "%s: XiP data header is not PT_LOAD\n", elf_path);
            return 1;
        }
        text_offset = p_offset;
        text_vaddr = p_vaddr;
        text_filesz = p_filesz;
        p_offset = rd32(ph[1] + 4);
        p_vaddr = rd32(ph[1] + 8);
        p_filesz = rd32(ph[1] + 16);
        p_memsz = rd32(ph[1] + 20);
        xip = 1;
    }

    if (entry_arg)
        e_entry = (uint32_t)strtoul(entry_arg, NULL, 16);

    if (xip && (text_vaddr - BANK_GUEST < 0x1000u ||
                text_vaddr - BANK_GUEST + text_filesz > VEC_IPTR_OFF)) {
        fprintf(stderr, "XiP text does not fit below the boot vectors\n");
        return 1;
    }
    if (xip && p_vaddr < 0x80000000u) {
        fprintf(stderr, "XiP RAM segment has invalid address 0x%08X\n", p_vaddr);
        return 1;
    }
    if (PAYLOAD_OFF + p_filesz > ROOTFS_OFF) {
        fprintf(stderr, "payload (%u bytes) exceeds the 48MiB kernel slot\n",
                p_filesz);
        return 1;
    }

    payload = read_segment(elf, p_offset, p_filesz, "RAM PT_LOAD");
    if (!payload) return 1;
    if (xip) {
        xip_text = read_segment(elf, text_offset, text_filesz,
                                "XiP text PT_LOAD");
        if (!xip_text) return 1;
    }
    fclose(elf);

    /* Workspace for the stub and initial kernel entry: RAM just past
     * the loaded image, word aligned — same shape link boot leaves. */
    boot_wptr = (p_vaddr + (p_memsz > p_filesz ? p_memsz : p_filesz) +
                 0x1000u + 3u) & ~3u;

    /* Assemble the stub:  A=len B=dst C=src -> move; then gcall entry. */
    stublen = 0;
    ldc(PAYLOAD_GUEST);          /* -> C after two more pushes */
    ldc(p_vaddr);                /* dst */
    ldc(p_filesz);               /* len */
    opr(0x4A);                   /* move (one host memcpy in t4) */
    ldc(e_entry);
    opr(0x06);                   /* gcall */

    sd = fopen(sd_path, "r+b");
    if (!sd) sd = fopen(sd_path, "w+b");
    if (!sd) { perror(sd_path); return 1; }

    /* Ensure the file covers the full card so t4's fread sees it all. */
    fseek(sd, 0, SEEK_END);
    if (ftell(sd) < (long)CARD_SIZE) {
        fseek(sd, CARD_SIZE - 1, SEEK_SET);
        fputc(0, sd);
    }

    fseek(sd, STUB_CARD_OFF, SEEK_SET);
    fwrite(stub, 1, stublen, sd);

    if (xip) {
        fseek(sd, text_vaddr - BANK_GUEST, SEEK_SET);
        n = (long)fwrite(xip_text, 1, text_filesz, sd);
        if (n != (long)text_filesz) { perror("XiP text write"); return 1; }
    }

    fseek(sd, PAYLOAD_OFF, SEEK_SET);
    n = (long)fwrite(payload, 1, p_filesz, sd);
    if (n != (long)p_filesz) { perror("payload write"); return 1; }

    put_le32(sd, VEC_IPTR_OFF, STUB_XIP_ADDR);
    put_le32(sd, VEC_WPTR_OFF, boot_wptr);
    write_mbr(sd);
    if (rootfs_path && copy_file(sd, ROOTFS_OFF, rootfs_path, ROOTFS_SIZE) < 0)
        return 1;
    fclose(sd);
    free(payload);
    free(xip_text);

    printf("%s: %s payload %u bytes @card+0x%X (load 0x%08X entry 0x%08X)\n"
           "  stub %d bytes @card+0x%X (XiP 0x%08X)  BootWptr 0x%08X\n",
           sd_path, xip ? "RAM" : "flat", p_filesz, PAYLOAD_OFF,
           p_vaddr, e_entry,
           stublen, STUB_CARD_OFF, STUB_XIP_ADDR, boot_wptr);
    if (xip)
        printf("  XiP text %u bytes @card+0x%X (guest 0x%08X)\n",
               text_filesz, text_vaddr - BANK_GUEST, text_vaddr);
    printf("  rootfs partition: card+0x%X, %uMiB%s%s\n",
           ROOTFS_OFF, ROOTFS_SIZE / (1024u * 1024u),
           rootfs_path ? ", populated from " : "",
           rootfs_path ? rootfs_path : "");
    return 0;
}
