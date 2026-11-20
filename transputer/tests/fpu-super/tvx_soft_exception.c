/* Called by the same trap probes used for native TVX instructions. */
unsigned tvx_soft_exception(unsigned *io)
{
    if (io[18]==0x1ce) return __bf16add2(io[0],io[2]);
    if (io[18]==0x1cd) return __bf16mul2(io[0],io[2]);
    {
        union {float f;unsigned u;} acc,r;
        acc.u=io[3];
        r.f=__bf16dot(io,io+2,1,acc.f);
        return r.u;
    }
}
