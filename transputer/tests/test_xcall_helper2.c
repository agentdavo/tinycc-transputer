struct xnode {
    unsigned long tag;
    unsigned long vals[4];
};

struct xzone {
    char pad[2048];
    struct xnode nodes[6];
};

static struct xzone xz;
static unsigned long reloc_pad[256];

const unsigned long helper_magic[4] = {
    0x10203040u,
    0x99aabbccu,
    0x55667788u,
    0xddeeff00u
};

static unsigned long helper2_noise(unsigned long x)
{
    x += reloc_pad[(x >> 2) & 7u];
    x ^= 0x13579bdfu;
    return x;
}

unsigned long helper2_base(void)
{
    return (unsigned long)&xz;
}

unsigned long helper2_node_tail(int idx)
{
    return (unsigned long)&xz.nodes[idx].vals[3];
}

unsigned long helper2_fill(unsigned char *buf, unsigned long count, unsigned char value)
{
    unsigned char *p = buf;

    while (count--) {
        *p++ = value;
    }
    return (unsigned long)p;
}

unsigned long helper2_mix(unsigned long x)
{
    return (helper2_noise(x) ^ 0x13579bdfu ^ helper_magic[2]) + helper_magic[1];
}
