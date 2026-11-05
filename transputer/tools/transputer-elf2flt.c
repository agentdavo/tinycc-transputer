/*
 * transputer-elf2flt — Convert transputer ELF to bFLT format
 *
 * Reads a statically-linked transputer ELF executable (Machine: 0x325)
 * and produces a bFLT (binary flat) file for binfmt_flat on NOMMU Linux.
 *
 * Scans the binary for LDC prefix sequences and data words that reference
 * addresses within the ELF segment, and emits bFLT relocations so the
 * flat loader can fix them up at load time.
 *
 * Relocation entry format (32-bit, big-endian in the bFLT file):
 *   bit 31 = 1: variable-length LDC prefix sequence
 *   bit 31 = 0: standard 32-bit data word
 *   bits 0-30:  offset from start of text in the bFLT image
 *
 * Build:  make transputer-elf2flt
 * Usage:  ./transputer-elf2flt [-k] input.elf output.flt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>  /* htonl */
#include "../../elf.h"

/* bFLT header — all fields in network (big-endian) byte order */
struct flat_hdr {
	char     magic[4];
	uint32_t rev;
	uint32_t entry;
	uint32_t data_start;
	uint32_t data_end;
	uint32_t bss_end;
	uint32_t stack_size;
	uint32_t reloc_start;
	uint32_t reloc_count;
	uint32_t flags;
	uint32_t build_date;
	uint32_t filler[5];
};

#define FLAT_VERSION    4
#define FLAT_FLAG_RAM   0x0001
#define FLAT_FLAG_KTRACE 0x0010

/* Transputer prefix encoding constants */
#define TP_PFIX  0x2
#define TP_NFIX  0x6
#define TP_LDC   0x4

/* bFLT relocation type flag */
#define FLAT_RELOC_TYPE_LDC  0x80000000u

static int tp_is_prefix(uint8_t b)
{
	return (b >> 4) == TP_PFIX || (b >> 4) == TP_NFIX;
}

/*
 * Decode a transputer prefix+opcode sequence starting at buf[0..len-1].
 * Returns the decoded operand value.
 *
 * Uses hardware semantics:
 *   PFIX n: Oreg = (Oreg | n) << 4
 *   NFIX n: Oreg = ~(Oreg | n) << 4
 *   final:  Operand = Oreg | n
 */
static uint32_t tp_decode_seq(const uint8_t *buf, int len)
{
	uint32_t oreg = 0;
	int i;

	for (i = 0; i < len - 1; i++) {
		uint32_t nibble = buf[i] & 0xF;
		if ((buf[i] >> 4) == TP_NFIX)
			oreg = (~(oreg | nibble)) << 4;
		else
			oreg = (oreg | nibble) << 4;
	}
	return oreg | (buf[len - 1] & 0xF);
}

/*
 * Encode an unsigned value into a prefix+LDC sequence of exactly `len` bytes.
 * Uses PFIX only (unsigned encoding for absolute addresses).
 */
static void tp_encode_ldc(uint8_t *buf, int len, uint32_t val)
{
	int i;
	/* Pad with PFIX 0, then encode top-down */
	memset(buf, 0x20, len - 1);  /* PFIX 0 */

	/* Count needed nibbles */
	int needed = 0;
	uint32_t tmp = val;
	do { needed++; tmp >>= 4; } while (tmp != 0);

	int pad = len - needed;
	if (pad < 0) pad = 0;

	for (i = 0; i < needed - 1 && pad + i < len - 1; i++) {
		int shift = (needed - 1 - i) * 4;
		buf[pad + i] = 0x20 | ((val >> shift) & 0xF);
	}
	buf[len - 1] = (TP_LDC << 4) | (val & 0xF);
}

static void usage(const char *prog)
{
	fprintf(stderr, "usage: %s [-k] <input.elf> <output.flt>\n", prog);
	fprintf(stderr, "  -k   enable kernel trace (FLAT_FLAG_KTRACE)\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int ktrace = 0;
	int argi = 1;

	while (argi < argc && argv[argi][0] == '-') {
		if (strcmp(argv[argi], "-k") == 0)
			ktrace = 1;
		else
			usage(argv[0]);
		argi++;
	}

	if (argc - argi < 2)
		usage(argv[0]);

	const char *elf_path = argv[argi];
	const char *flt_path = argv[argi + 1];

	/* Read ELF file */
	FILE *fi = fopen(elf_path, "rb");
	if (!fi) { perror(elf_path); return 1; }

	fseek(fi, 0, SEEK_END);
	long file_size = ftell(fi);
	fseek(fi, 0, SEEK_SET);

	uint8_t *buf = malloc(file_size);
	if (!buf || fread(buf, 1, file_size, fi) != (size_t)file_size) {
		perror("read"); return 1;
	}
	fclose(fi);

	/* Parse ELF header */
	Elf32_Ehdr *ehdr = (Elf32_Ehdr *)buf;
	if (memcmp(ehdr->e_ident, "\x7f" "ELF", 4) != 0) {
		fprintf(stderr, "Not an ELF file\n"); return 1;
	}
	if (ehdr->e_machine != 0x325) {
		fprintf(stderr, "Not a transputer ELF (machine=0x%x)\n",
			ehdr->e_machine);
		return 1;
	}

	uint32_t entry_vaddr = ehdr->e_entry;
	fprintf(stderr, "ELF entry: 0x%08x\n", entry_vaddr);

	/* Validate program header table bounds */
	if (ehdr->e_phoff + ehdr->e_phnum * sizeof(Elf32_Phdr) > (size_t)file_size) {
		fprintf(stderr, "ELF program header table extends past file end\n");
		return 1;
	}

	/* Find the PT_LOAD segment */
	Elf32_Phdr *phdr = (Elf32_Phdr *)(buf + ehdr->e_phoff);
	uint32_t seg_vaddr = 0, seg_offset = 0, seg_filesz = 0, seg_memsz = 0;
	int found = 0;

	for (int i = 0; i < ehdr->e_phnum; i++) {
		if (phdr[i].p_type == PT_LOAD) {
			if (found) {
				fprintf(stderr, "Warning: multiple PT_LOAD segments; "
					"using first\n");
				break;
			}
			seg_vaddr  = phdr[i].p_vaddr;
			seg_offset = phdr[i].p_offset;
			seg_filesz = phdr[i].p_filesz;
			seg_memsz  = phdr[i].p_memsz;
			found = 1;
		}
	}

	if (!found) {
		fprintf(stderr, "No PT_LOAD segment found\n"); return 1;
	}

	fprintf(stderr, "PT_LOAD: vaddr=0x%08x offset=0x%x filesz=%u memsz=%u\n",
		seg_vaddr, seg_offset, seg_filesz, seg_memsz);

	if (entry_vaddr < seg_vaddr ||
	    entry_vaddr >= seg_vaddr + seg_filesz) {
		fprintf(stderr, "Warning: entry 0x%08x outside PT_LOAD range\n",
			entry_vaddr);
	}

	if ((uint64_t)seg_offset + seg_filesz > (uint64_t)file_size) {
		fprintf(stderr, "PT_LOAD (off=0x%x filesz=0x%x) extends past file end (0x%lx)\n",
			seg_offset, seg_filesz, (unsigned long)file_size);
		free(buf);
		return 1;
	}

	uint8_t *image = buf + seg_offset;
	uint32_t image_size = seg_filesz;
	uint32_t bss_size = seg_memsz > seg_filesz ? seg_memsz - seg_filesz : 0;

	/* Entry point as offset from start of loaded image */
	uint32_t entry_offset = entry_vaddr - seg_vaddr;

	/* ---- Scan for relocations ---- */
	uint32_t reloc_cap = 256;
	uint32_t reloc_count = 0;
	uint32_t *relocs = malloc(reloc_cap * sizeof(uint32_t));

	/*
	 * Scan for LDC prefix sequences (R_TRANSPUTER_LDC_ABS).
	 *
	 * The transputer linker emits VARIABLE-LENGTH LDC_ABS sequences: a run of
	 * PFIX (0x2x) / NFIX (0x6x) prefix bytes terminated by an LDC (0x4x)
	 * opcode. The compiler lays down a 9-byte placeholder, but when the linker
	 * patches an address it may "walk back" over preceding PFIX 0 padding,
	 * producing chains anywhere from ~8 up to ~17 bytes (and occasionally with
	 * an embedded NFIX). A fixed 9-byte window scan (the old approach) recorded
	 * ONLY the exactly-9-byte chains and silently dropped every longer one —
	 * leaving hundreds of absolute-address loads un-relocated in large binaries
	 * like BusyBox, which then jump/deref link-time addresses and crash.
	 *
	 * We therefore scan each maximal prefix run (byte before is not a prefix)
	 * and, if it terminates in LDC and decodes to an in-image address, record
	 * the chain START offset. The bFLT loader re-derives the chain width in
	 * place, so no width needs to be stored here.
	 */
	#define LDC_MAX_LEN 20

	for (uint32_t off = 0; off < image_size; ) {
		if (!tp_is_prefix(image[off])) {
			off++;
			continue;
		}

		/* Start of a maximal prefix run (previous byte is non-prefix). */
		uint32_t start = off;
		uint32_t k = off;
		while (k < image_size && (k - start) < (LDC_MAX_LEN - 1) &&
		       tp_is_prefix(image[k]))
			k++;

		/* k now indexes the first non-prefix byte (candidate opcode). */
		if (k < image_size && (image[k] >> 4) == TP_LDC) {
			int seq_len = (int)(k - start + 1);
			uint32_t uval = tp_decode_seq(&image[start], seq_len);

			if (uval >= seg_vaddr && uval < seg_vaddr + seg_memsz) {
				uint32_t img_off = uval - seg_vaddr;

				/* bFLT stores an image-relative target at each
				 * relocation site. The loader's calc_reloc() turns
				 * this back into a runtime address. */
				tp_encode_ldc(&image[start], seq_len, img_off);

				if (reloc_count >= reloc_cap) {
					reloc_cap *= 2;
					relocs = realloc(relocs,
							 reloc_cap * sizeof(uint32_t));
				}
				relocs[reloc_count++] = start | FLAT_RELOC_TYPE_LDC;

				fprintf(stderr,
					"  LDC_ABS reloc: off=0x%x len=%d val=0x%08x -> img_off=0x%x\n",
					start, seq_len, uval, img_off);
			}
			off = k + 1;  /* resume after the LDC opcode */
		} else {
			off = k + 1;  /* skip this run and its terminating opcode */
		}
	}

	/*
	 * Scan for 32-bit data relocations (R_TRANSPUTER_32).
	 *
	 * Only scan positions that are clearly in the data area —
	 * after the last known code (identified by looking for a region
	 * that doesn't contain valid prefix/opcode patterns).
	 *
	 * Heuristic: data starts at the first 4-byte-aligned position
	 * after the last LDC_ABS relocation target (since string literals
	 * are typically placed right after code).
	 */
	{
		/* Find approximate data boundary: after the highest address
		 * referenced by any LDC_ABS relocation */
		uint32_t data_boundary = image_size / 2;  /* conservative default */
		for (uint32_t j = 0; j < reloc_count; j++) {
			uint32_t roff = relocs[j] & 0x7FFFFFFF;
			if (roff + LDC_MAX_LEN > data_boundary)
				data_boundary = roff + LDC_MAX_LEN;
		}
		/* Align to 4 bytes */
		data_boundary = (data_boundary + 3) & ~3u;

		for (uint32_t off = data_boundary; off + 4 <= image_size; off += 4) {
			uint32_t val = image[off] | (image[off+1] << 8) |
				       (image[off+2] << 16) | (image[off+3] << 24);

			if (val >= seg_vaddr && val < seg_vaddr + seg_memsz) {
				uint32_t img_off = val - seg_vaddr;

				/* The generic loader expects a big-endian,
				 * image-relative target at an R32 relocation site. */
				image[off]     = (uint8_t)(img_off >> 24);
				image[off + 1] = (uint8_t)(img_off >> 16);
				image[off + 2] = (uint8_t)(img_off >> 8);
				image[off + 3] = (uint8_t)img_off;

				if (reloc_count >= reloc_cap) {
					reloc_cap *= 2;
					relocs = realloc(relocs, reloc_cap * sizeof(uint32_t));
				}
				relocs[reloc_count++] = off;  /* no LDC flag = R32 type */

				fprintf(stderr, "  R32 reloc: off=0x%x val=0x%08x -> img_off=0x%x\n",
					off, val, img_off);
			}
		}
	}

	{
		unsigned ldc_count = 0, r32_count = 0;
		for (unsigned j = 0; j < reloc_count; j++) {
			if (relocs[j] & FLAT_RELOC_TYPE_LDC) ldc_count++;
			else r32_count++;
		}
		fprintf(stderr, "Found %u relocations (%u LDC, %u R32)\n",
			reloc_count, ldc_count, r32_count);
	}

	/* ---- Build bFLT output ---- */

	/* Standard revision-4 bFLT layout:
	 *   [header] [text/rodata/init-data image] [relocation table]
	 * Keeping reloc_count and reloc_start in the header is essential:
	 * generic binfmt_flat uses those fields to find and apply the table. */
	uint32_t hdr_size = sizeof(struct flat_hdr);
	uint32_t data_start_off = hdr_size + image_size;
	uint32_t data_end_off   = data_start_off;
	uint32_t bss_end_off    = data_end_off + bss_size;
	uint32_t reloc_start_off = data_end_off;
	/* binfmt_flat reserves exactly this much stack below the argument
	 * tables.  TinyCC keeps every local and spill in the workspace, so
	 * transputer frames are large: 256 KiB on top of the usual 8 KiB. */
	uint32_t stack_size = 8192 + 256 * 1024;

	struct flat_hdr hdr;
	memset(&hdr, 0, sizeof(hdr));
	memcpy(hdr.magic, "bFLT", 4);
	hdr.rev         = htonl(FLAT_VERSION);
	hdr.entry       = htonl(hdr_size + entry_offset);
	hdr.data_start  = htonl(data_start_off);
	hdr.data_end    = htonl(data_end_off);
	hdr.bss_end     = htonl(bss_end_off);
	hdr.stack_size  = htonl(stack_size);
	hdr.reloc_start = htonl(reloc_start_off);
	hdr.reloc_count = htonl(reloc_count);
	hdr.flags       = htonl(FLAT_FLAG_RAM | (ktrace ? FLAT_FLAG_KTRACE : 0));
	hdr.build_date  = htonl(seg_vaddr);

	/* Write output */
	FILE *fo = fopen(flt_path, "wb");
	if (!fo) { perror(flt_path); return 1; }

	if (fwrite(&hdr, sizeof(hdr), 1, fo) != 1) {
		perror("fwrite header"); fclose(fo); return 1;
	}

	/* Write the relocated image followed by the standard table. */
	if (fwrite(image, 1, image_size, fo) != image_size) {
		perror("fwrite image"); fclose(fo); return 1;
	}
	for (uint32_t i = 0; i < reloc_count; i++) {
		uint32_t off_be = htonl(relocs[i]);
		if (fwrite(&off_be, 4, 1, fo) != 1) {
			perror("fwrite relocation"); fclose(fo); return 1;
		}
	}

	fclose(fo);
	free(buf);
	free(relocs);

	fprintf(stderr, "Created %s: entry=0x%x text=%u data=%u bss=%u "
		"relocs=%u stack=%u\n",
		flt_path, hdr_size + entry_offset,
		image_size, 0, bss_size, reloc_count, stack_size);

	return 0;
}
