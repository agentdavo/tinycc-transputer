extern unsigned long helper2_node_tail(int idx);
extern unsigned long helper2_fill(unsigned char *buf, unsigned long count, unsigned char value);
extern unsigned long helper2_mix(unsigned long x);
extern const unsigned long helper_magic[4];

unsigned long helper1_tail_via_call(int idx)
{
    return helper2_node_tail(idx);
}

unsigned long helper1_tail_via_fp(int idx)
{
    unsigned long (*fp)(int);

    fp = helper2_node_tail;
    return fp(idx);
}

unsigned long helper1_fill_and_sum(unsigned char *buf, unsigned long count)
{
    unsigned char *end;

    end = (unsigned char *)helper2_fill(buf, count, 0x3cu);
    return (unsigned long)(end - buf);
}

unsigned long helper1_mix(unsigned long x)
{
    return helper2_mix(x);
}

unsigned long helper1_magic3(void)
{
    return helper_magic[3];
}
