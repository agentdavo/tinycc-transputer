#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../elf.h"

/* Simple network loader utility. It converts a linked ELF binary
   into a chunked BTL stream accepted by the TEMU emulator. The
   output begins with a single bootstrap-length byte. We emit a
   zero-length bootstrap so that the payload loads at MemStart. */

static uint32_t be32(uint32_t v)
{
    return ((v & 0xff) << 24) | ((v & 0xff00) << 8)
           | ((v >> 8) & 0xff00) | ((v >> 24) & 0xff);
}

/**
 * @brief Helper function to write data in the Transputer boot format.
 *
 * Data is broken into chunks of 255 bytes or less. Each chunk is
 * prefixed by a single byte indicating its length.
 *
 * @param fo The output file handle.
 * @param data A pointer to the data to write. If NULL, this function will write zero bytes.
 * @param size The total number of bytes to write.
 */
static int write_chunked_data(FILE *fo, const uint8_t *data, long size) {
    long written = 0;
    uint8_t zero_chunk[255] = {0}; // Pre-filled zero buffer for efficiency

    while (written < size) {
        long remaining = size - written;
        int chunk_size = (remaining > 255) ? 255 : (int)remaining;
        const uint8_t *source_buf;
        size_t items_written;

        // Write the 1-byte chunk length
        if (fputc(chunk_size, fo) == EOF) {
            perror("FATAL: Failed to write chunk length to BTL file");
            return -1;
        }

        // Write the chunk data
        source_buf = data ? (data + written) : zero_chunk;
        items_written = fwrite(source_buf, 1, chunk_size, fo);

        if (items_written != (size_t)chunk_size) {
            perror("FATAL: Failed to write chunk data to BTL file");
            fprintf(stderr, "Expected to write %d bytes, but only wrote %zu bytes\n",
                    chunk_size, items_written);
            return -1;
        }

        written += chunk_size;
    }
    return 0;
}

/**
 * @brief Comparison function for qsort to sort ELF Program Headers by p_paddr.
 */
static int compare_phdrs(const void *a, const void *b) {
    Elf32_Phdr *ph_a = *(Elf32_Phdr **)a;
    Elf32_Phdr *ph_b = *(Elf32_Phdr **)b;
    if (ph_a->p_paddr < ph_b->p_paddr) return -1;
    if (ph_a->p_paddr > ph_b->p_paddr) return 1;
    return 0;
}

/**
 * @brief Main logic for converting a single ELF file to a BTL boot file.
 */
int netload_main(int argc, char **argv) {
    const char *in_file;
    const char *out_file;
    FILE *fi;
    FILE *fo;
    long file_size;
    uint8_t *elf_buf;
    Elf32_Ehdr *eh;
    Elf32_Phdr *ph_table;
    Elf32_Phdr **load_segments;
    int load_count;
    uint32_t memstart;
    uint32_t base_addr;
    uint32_t current_paddr;
    int i;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <input.elf> <output.btl>\n", argv[0]);
        return 1;
    }

    in_file = argv[1];
    out_file = argv[2];

    fi = fopen(in_file, "rb");
    if (!fi) {
        perror(in_file);
        return 1;
    }

    fseek(fi, 0, SEEK_END);
    file_size = ftell(fi);
    fseek(fi, 0, SEEK_SET);

    elf_buf = malloc(file_size);
    if (!elf_buf || fread(elf_buf, 1, file_size, fi) != (size_t)file_size) {
        perror("Failed to read ELF file");
        fclose(fi);
        free(elf_buf);
        return 1;
    }
    fclose(fi);

    eh = (Elf32_Ehdr *)elf_buf;

    /* Validate ELF magic and machine type */
    if (file_size < (long)sizeof(Elf32_Ehdr) ||
        memcmp(eh->e_ident, "\x7f" "ELF", 4) != 0) {
        fprintf(stderr, "ERROR: %s is not a valid ELF file\n", in_file);
        free(elf_buf);
        return 1;
    }
    if (eh->e_machine != 0x325) {
        fprintf(stderr, "ERROR: Not a transputer ELF (machine=0x%x)\n", eh->e_machine);
        free(elf_buf);
        return 1;
    }

    /* Validate program header table bounds */
    if (eh->e_phoff + eh->e_phnum * sizeof(Elf32_Phdr) > (size_t)file_size) {
        fprintf(stderr, "ERROR: Program header table extends past file end\n");
        free(elf_buf);
        return 1;
    }

    ph_table = (Elf32_Phdr *)(elf_buf + eh->e_phoff);

    // --- Collect and sort all loadable segments ---
    load_segments = malloc(eh->e_phnum * sizeof(Elf32_Phdr *));
    load_count = 0;
    for (i = 0; i < eh->e_phnum; i++) {
        if (ph_table[i].p_type == PT_LOAD && ph_table[i].p_filesz > 0) {
            load_segments[load_count++] = &ph_table[i];
        }
    }

    if (load_count == 0) {
        fprintf(stderr, "ERROR: No loadable segments found in ELF file.\n");
        free(load_segments);
        free(elf_buf);
        return 1;
    }

    qsort(load_segments, load_count, sizeof(Elf32_Phdr *), compare_phdrs);

    // --- Verify base address and open output file ---
    memstart = 0x80000070; // T800 emulator's MemStart address
    base_addr = load_segments[0]->p_paddr;

    fprintf(stderr, "Found %d loadable segments. Image base address: 0x%x\n", load_count, base_addr);

    // A T414's MemStart (-mt414 links there): boot it with t4 -s414
    if (base_addr == 0x80000048)
        memstart = base_addr;
    if (base_addr != memstart) {
        fprintf(stderr, "ERROR: Image base address 0x%x does not match emulator's MemStart 0x%x\n",
                base_addr, memstart);
        free(load_segments);
        free(elf_buf);
        return 1;
    }

    fo = fopen(out_file, "wb");
    if (!fo) {
        perror(out_file);
        free(load_segments);
        free(elf_buf);
        return 1;
    }

    // --- Write bootstrap length (0 = no bootstrap) ---
    if (fputc(0, fo) == EOF) {
        perror("FATAL: Failed to write bootstrap length to BTL file");
        fclose(fo);
        free(load_segments);
        free(elf_buf);
        return 1;
    }

    // --- Write segments to BTL file, preserving memory layout ---
    current_paddr = base_addr;
    for (i = 0; i < load_count; i++) {
        Elf32_Phdr *ph = load_segments[i];
        uint32_t call_site_mem;
        int j;

        // **CRITICAL FIX**: If there is a gap between the last segment and this one,
        // fill it with zero bytes to maintain correct memory offsets.
        if (ph->p_paddr > current_paddr) {
            long gap_size = ph->p_paddr - current_paddr;
            fprintf(stderr, "  Padding gap of %ld bytes at address 0x%x\n", gap_size, current_paddr);
            if (write_chunked_data(fo, NULL, gap_size) < 0) {
                fclose(fo);
                free(load_segments);
                free(elf_buf);
                return 1;
            }
        }

        fprintf(stderr, "  Writing segment: file offset 0x%x -> mem 0x%x, p_filesz=%u p_memsz=%u\n",
                ph->p_offset, ph->p_paddr, ph->p_filesz, ph->p_memsz);

        // Debug: Check call site bytes at VA 0x8000210b
        call_site_mem = 0x8000210b;
        if (ph->p_paddr <= call_site_mem && (ph->p_paddr + ph->p_filesz) > call_site_mem) {
            uint32_t offset_in_seg = call_site_mem - ph->p_paddr;
            uint32_t file_off = ph->p_offset + offset_in_seg;
            fprintf(stderr, "  DEBUG: call site at VA 0x8000210b, seg offset 0x%x, file offset 0x%x\n", 
                    offset_in_seg, file_off);
            fprintf(stderr, "  DEBUG: Bytes in ELF buffer: ");
            for (j = 0; j < 9; j++)
                fprintf(stderr, "%02x ", elf_buf[file_off + j]);
            fprintf(stderr, "\n");
        }

        // Debug: check a few bytes at the start of the segment
        fprintf(stderr, "  DEBUG: First 16 bytes of segment: ");
        for (j = 0; j < 16; j++)
            fprintf(stderr, "%02x ", elf_buf[ph->p_offset + j]);
        fprintf(stderr, "\n");
        
        // Write the actual segment data from the file (initialized data: .text, .data, .rodata)
        if (write_chunked_data(fo, elf_buf + ph->p_offset, ph->p_filesz) < 0) {
            fclose(fo);
            free(load_segments);
            free(elf_buf);
            return 1;
        }

        // **CRITICAL BUG FIX**: Handle .bss section (uninitialized data)
        // If p_memsz > p_filesz, the difference is the .bss section which must be zero-filled
        if (ph->p_memsz > ph->p_filesz) {
            long bss_size = ph->p_memsz - ph->p_filesz;
            fprintf(stderr, "  Zero-filling .bss section: %ld bytes at address 0x%x\n",
                    bss_size, current_paddr + ph->p_filesz);
            if (write_chunked_data(fo, NULL, bss_size) < 0) {
                fclose(fo);
                free(load_segments);
                free(elf_buf);
                return 1;
            }
        }

        // Advance memory pointer by TOTAL memory size (p_memsz), not just file size!
        current_paddr = ph->p_paddr + ph->p_memsz;
    }

    // Terminate the bootstrap sequence with a zero-length chunk
    if (fputc(0, fo) == EOF) {
        perror("FATAL: Failed to write terminating zero to BTL file");
        fclose(fo);
        free(load_segments);
        free(elf_buf);
        return 1;
    }

    if (fclose(fo) != 0) {
        perror("FATAL: Failed to close BTL file");
        free(load_segments);
        free(elf_buf);
        return 1;
    }

    fprintf(stderr, "Successfully created boot file '%s'\n", out_file);

    free(load_segments);
    free(elf_buf);
    return 0;
}

/* Multi-node deployment functionality from transputer-deploy.c */

struct Node {
    char name[32];
    char *elf;
    uint8_t *img;
    uint32_t size;
    uint32_t entry;
    struct Node *next;
};

struct Link {
    char n1[32];
    int p1;
    char n2[32];
    int p2;
    struct Link *next;
};


static int write_links(FILE *fo, struct Node *nodes, struct Link *links) {
    unsigned lcount = 0;
    Elf32_Phdr *ph_a;
    Elf32_Phdr *ph_b;
    uint32_t lcount_be;
    uint8_t entry[4];
    int idx1, idx2, idx;
    struct Link *l;
    struct Node *n;

    for (l = links; l; l = l->next)
        lcount++;
    lcount_be = be32(lcount);

    if (fwrite(&lcount_be, 1, 4, fo) != 4) {
        perror("FATAL: Failed to write link count");
        return -1;
    }

    for (l = links; l; l = l->next) {
        idx1 = -1;
        idx2 = -1;
        idx = 0;
        for (n = nodes; n; n = n->next, idx++) {
            if (!strcmp(n->name, l->n1))
                idx1 = idx;
            if (!strcmp(n->name, l->n2))
                idx2 = idx;
        }
        if (idx1 < 0 || idx2 < 0)
            continue; /* ignore invalid links */
        entry[0] = idx1;
        entry[1] = l->p1;
        entry[2] = idx2;
        entry[3] = l->p2;

        if (fwrite(entry, 1, 4, fo) != 4) {
            perror("FATAL: Failed to write link entry");
            return -1;
        }
    }
    /* Suppress unused variable warnings */
    (void)ph_a;
    (void)ph_b;
    return 0;
}

static void free_nodes(struct Node *head) {
    while (head) {
        struct Node *n = head->next;
        free(head->img);
        free(head->elf);
        free(head);
        head = n;
    }
}

static void usage_deploy(const char *prog) {
    fprintf(stderr, "usage: %s <config.txt> <output.bin>\n", prog);
}

static int load_elf(struct Node *n) {
    FILE *fi;
    long size;
    uint8_t *buf;
    Elf32_Ehdr *eh;
    uint32_t base;

    fi = fopen(n->elf, "rb");
    if (!fi) {
        perror(n->elf);
        return -1;
    }

    fseek(fi, 0, SEEK_END);
    size = ftell(fi);
    fseek(fi, 0, SEEK_SET);

    buf = malloc(size);
    if (!buf || fread(buf, 1, size, fi) != (size_t)size) {
        perror("read");
        fclose(fi);
        return -1;
    }
    fclose(fi);

    eh = (Elf32_Ehdr *)buf;
    if (eh->e_type != ET_REL) {
        fprintf(stderr, "Error: %s must be relocatable ELF\n", n->elf);
        free(buf);
        return -1;
    }

    /* Simple relocation - assume base address */
    base = 0x80000070;  /* T800 MemStart */
    n->entry = base;
    n->size = size; /* Simplified */
    n->img = buf;

    return 0;
}

static int deploy_main(int argc, char **argv) {
    const char *cfg;
    const char *out;
    FILE *fc;
    FILE *fo;
    struct Node *nodes = NULL, **tail = &nodes;
    struct Link *links = NULL, **ltail = &links;
    char line[256];
    unsigned count;
    uint32_t count_be;
    struct Node *n;
    struct Link *l;

    if (argc < 3) {
        usage_deploy(argv[0]);
        return 1;
    }

    cfg = argv[1];
    out = argv[2];
    fc = fopen(cfg, "r");
    if (!fc) {
        perror(cfg);
        return 1;
    }

    while (fgets(line, sizeof(line), fc)) {
        char *tok;
        if (line[0] == '#' || line[0] == '\n')
            continue;
        tok = strtok(line, " \t\n");
        if (!tok)
            continue;
        if (!strcmp(tok, "node")) {
            char *name = strtok(NULL, " \t\n");
            char *file = strtok(NULL, " \t\n");
            struct Node *node;
            if (!name || !file)
                continue;
            node = calloc(1, sizeof(*node));
            strncpy(node->name, name, sizeof(node->name)-1);
            node->elf = strdup(file);
            node->next = NULL;
            *tail = node;
            tail = &node->next;
        } else if (!strcmp(tok, "link")) {
            char *n1 = strtok(NULL, " \t\n");
            char *p1 = strtok(NULL, " \t\n");
            char *n2 = strtok(NULL, " \t\n");
            char *p2 = strtok(NULL, " \t\n");
            struct Link *link;
            if (!n1 || !p1 || !n2 || !p2)
                continue;
            link = calloc(1, sizeof(*link));
            strncpy(link->n1, n1, sizeof(link->n1)-1);
            link->p1 = atoi(p1);
            strncpy(link->n2, n2, sizeof(link->n2)-1);
            link->p2 = atoi(p2);
            link->next = NULL;
            *ltail = link;
            ltail = &link->next;
        }
    }
    fclose(fc);

    count = 0;
    for (n = nodes; n; n = n->next) {
        if (load_elf(n) < 0) {
            free_nodes(nodes);
            return 1;
        }
        count++;
    }

    fo = fopen(out, "wb");
    if (!fo) {
        perror(out);
        free_nodes(nodes);
        return 1;
    }

    count_be = be32(count);
    if (fwrite(&count_be, 1, 4, fo) != 4) {
        perror("FATAL: Failed to write node count");
        fclose(fo);
        free_nodes(nodes);
        return 1;
    }

    for (n = nodes; n; n = n->next) {
        uint8_t len = strlen(n->name);
        uint32_t size_be = be32(n->size);
        uint32_t entry_be = be32(n->entry);

        if (fwrite(&len, 1, 1, fo) != 1) {
            perror("FATAL: Failed to write node name length");
            fclose(fo);
            free_nodes(nodes);
            return 1;
        }
        if (fwrite(n->name, 1, len, fo) != len) {
            perror("FATAL: Failed to write node name");
            fclose(fo);
            free_nodes(nodes);
            return 1;
        }
        if (fwrite(&size_be, 1, 4, fo) != 4) {
            perror("FATAL: Failed to write node size");
            fclose(fo);
            free_nodes(nodes);
            return 1;
        }
        if (fwrite(&entry_be, 1, 4, fo) != 4) {
            perror("FATAL: Failed to write node entry point");
            fclose(fo);
            free_nodes(nodes);
            return 1;
        }
        if (fwrite(n->img, 1, n->size, fo) != n->size) {
            perror("FATAL: Failed to write node image");
            fclose(fo);
            free_nodes(nodes);
            return 1;
        }
    }

    /* embed link table after the boot images */
    if (write_links(fo, nodes, links) < 0) {
        fclose(fo);
        free_nodes(nodes);
        return 1;
    }

    if (fclose(fo) != 0) {
        perror("FATAL: Failed to close output file");
        free_nodes(nodes);
        return 1;
    }
    free_nodes(nodes);
    /* free parsed links */
    while (links) {
        l = links->next;
        free(links);
        links = l;
    }
    return 0;
}

int main(int argc, char **argv)
{
    /* Check if this is deployment mode (config file) */
    if (argc >= 3) {
        const char *arg1 = argv[1];
        size_t len = strlen(arg1);
        if (len > 4 && strcmp(arg1 + len - 4, ".txt") == 0) {
            return deploy_main(argc, argv);
        }
    }

    /* Single-node BTL conversion */
    return netload_main(argc, argv);
}
