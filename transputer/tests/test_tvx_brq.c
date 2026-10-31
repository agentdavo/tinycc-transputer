/*
 * Target: -mt9000tvx
 * test_tvx_brq.c - on-target check for the TVX block-rotation instructions
 * (VHAD32, MXQROT, MXQROTBF).
 *
 * These implement the BRQ recipe from "Block Rotation is All You Need for
 * MXFP4 Quantization" (arXiv:2511.04214): a 32-point randomised Hadamard
 * applied *within* each MX block rather than globally across the tensor.
 *
 * Rather than comparing against a table of expected bits, this checks the
 * mathematical properties that make the instruction usable, each of which
 * would fail differently if the butterfly network, the sign application, the
 * normalisation or the split-half packing were wrong:
 *
 *   1. involution   - with mask 0, R = H/sqrt(32) and R*R = I, so rotating
 *                     twice returns the input.
 *   2. orthogonality- the L2 norm is unchanged (this is what lets the weight
 *                     side fold R in offline).
 *   3. dot preservation - dot(Rx, Rw) == dot(x, w).  If this failed, the
 *                     whole scheme would be wrong regardless of accuracy.
 *   4. fusion       - MXQROT equals VHAD32 followed by tvx_quantize_block,
 *                     byte for byte.  This pins the fused form to the
 *                     composition of the two parts it replaces.
 *   5. bf16 path    - MXQROTBF equals MXQROT fed the widened bf16 values.
 *   6. outlier win  - accumulated over many outlier-bearing blocks, rotating
 *                     before quantising reduces the reconstruction error.
 *                     This is the paper's actual claim, at block scale; it
 *                     is statistical, so it is measured, not asserted on a
 *                     single block.
 *
 * Build & run: see transputer/TVX-ISA.md section 6.
 */

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c){UART_DATA=(unsigned)(unsigned char)c;}
static void us(const char*s){while(*s)up(*s++);}
static void uh(unsigned v){int i;for(i=28;i>=0;i-=4){int n=(v>>i)&0xF;up(n<10?'0'+n:'a'+n-10);}}
static void ud(unsigned v){char b[12];int n=0;if(!v){up('0');return;}while(v){b[n++]='0'+v%10;v/=10;}while(n)up(b[--n]);}
static unsigned f2b(float f){union{float f;unsigned u;}v;v.f=f;return v.u;}
static float b2f(unsigned u){union{float f;unsigned u;}v;v.u=u;return v.f;}
static float fabsf_(float x){return x<0.0f?-x:x;}

static int bad=0, ck=0;
static void eq(const char*w,int i,unsigned g,unsigned e){
    ck++;
    if(g!=e){bad++;us("\nFAIL ");us(w);us("[");ud((unsigned)i);us("] got=");uh(g);us(" want=");uh(e);}
}
static void near(const char*w,int i,float g,float e,float tol){
    ck++;
    if(fabsf_(g-e)>tol){bad++;us("\nFAIL ");us(w);us("[");ud((unsigned)i);
        us("] got=");uh(f2b(g));us(" want=");uh(f2b(e));}
}
static void ok(const char*w,int cond){
    ck++;
    if(!cond){bad++;us("\nFAIL ");us(w);}
}

#define N 64                       /* two blocks */
#define NB (N/32)
#define NTRIAL 128                 /* blocks in the accuracy measurement */

static float X[N], W[N], RX[N], RW[N], TMP[N], BACK[N];
static unsigned char BQ[NB*17], BQ2[NB*17];
static unsigned short XB[N];

/* Reference quantise, matching tvx_quantize_block in transputer/include/tvx.h
 * but written out here so the test does not depend on that header. */
static void qblock(unsigned char *out, const float *in)
{
    float amax=0.0f, s; int i;
    for(i=0;i<32;i++){ float a=fabsf_(in[i]); if(a>amax) amax=a; }
    out[0]=(unsigned char)__mxe8m0of(f2b(amax));
    s=b2f(__mxe8m0tof32((unsigned)out[0]));
    for(i=0;i<16;i++){
        unsigned lo=__f32tomxfp4(f2b(in[i]/s));
        unsigned hi=__f32tomxfp4(f2b(in[i+16]/s));
        out[1+i]=(unsigned char)(lo|(hi<<4));
    }
}

static void dqblock(float *out, const unsigned char *b)
{
    float s=b2f(__mxe8m0tof32((unsigned)b[0])); int i;
    for(i=0;i<32;i++){
        unsigned n = (i<16) ? (unsigned)(b[1+i]&0x0F) : (unsigned)(b[1+i-16]>>4);
        out[i]=b2f(__mxfp4tof32(n))*s;
    }
}

static float dot(const float *a, const float *b, int n)
{ float t=0.0f; int i; for(i=0;i<n;i++) t=t+a[i]*b[i]; return t; }

int test_main(void)
{
    int i; unsigned mask; float d0, d1, e_plain, e_rot;

    for(i=0;i<N;i++){
        X[i] = (float)((i*37)%23 - 11) * 0.375f;
        W[i] = (float)((i*53)%17 -  8) * 0.5f;
    }

    /* --- 1. involution: mask 0, rotate twice, get the input back --- */
    __vsetlen(N);
    __vhad32(RX, X, 0u);
    __vhad32(BACK, RX, 0u);
    for(i=0;i<N;i++) near("involution", i, BACK[i], X[i], 1e-3f);

    /* --- 2. orthogonality: L2 norm preserved, with and without signs --- */
    mask = 0xA5C3F00Fu;
    __vhad32(RX, X, mask);
    {
        float n_in=dot(X,X,N), n_out=dot(RX,RX,N);
        ok("norm-ratio", n_out > n_in*0.999f && n_out < n_in*1.001f);
    }

    /* --- 3. dot preservation: the same R applied to both sides --- */
    __vhad32(RW, W, mask);
    d0 = dot(X, W, N);
    d1 = dot(RX, RW, N);
    near("dot-preserve", 0, d1, d0, 1e-2f);

    /* --- 4. fusion: MXQROT == VHAD32 then quantise, byte for byte --- */
    __mxqrot(BQ, X, mask);
    __vhad32(TMP, X, mask);
    for(i=0;i<NB;i++) qblock(&BQ2[i*17], &TMP[i*32]);
    for(i=0;i<NB*17;i++) eq("fusion", i, (unsigned)BQ[i], (unsigned)BQ2[i]);

    /* --- 5. bf16 source path agrees with the f32 one --- */
    for(i=0;i<N;i++){
        XB[i] = (unsigned short)__f32tobf16(f2b(X[i]));
        TMP[i] = b2f(__bf16tof32((unsigned)XB[i]));  /* what the lane holds */
    }
    __mxqrot(BQ, TMP, mask);
    __mxqrotbf(BQ2, XB, mask);
    for(i=0;i<NB*17;i++) eq("bf16path", i, (unsigned)BQ2[i], (unsigned)BQ[i]);

    /* --- 6. the paper's claim, measured: over many outlier-bearing blocks,
     *        rotating before quantising lowers the MXFP4 reconstruction
     *        error.  This is a statistical property, not a per-block one -
     *        a single block can go either way - so it is accumulated over
     *        NTRIAL blocks.
     *
     *        The comparison is made in each block's own basis.  R is
     *        orthogonal, so ||dequant(Q(Rx)) - Rx|| is the same error the
     *        caller would see after rotating back, and no inverse is needed.
     *
     *        Regime matters.  With no outliers rotation is a wash (there is
     *        nothing to spread), and with a single *extreme* outlier the
     *        plain path can win, because the outlier then dominates the
     *        signal energy and MXFP4 represents it well while everything
     *        else was going to flush to zero either way.  The interesting
     *        and realistic case - the one BRQ targets - is a bulk of
     *        moderate values with an outlier around 10x, which is what is
     *        generated here. --- */
    {
        /* xorshift32, deliberately add-free: the transputer ADC instruction
         * is the *error-checking* add, so an LCG's `* a + c` sets the Error
         * flag whenever the addition overflows and the emulator's -se stops
         * the run.  Shifts and xors avoid the issue entirely. */
        unsigned rng = 0x12345677u;
        int t;
        e_plain = 0.0f; e_rot = 0.0f;
        for(t=0;t<NTRIAL;t++){
            for(i=0;i<32;i++){
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                TMP[i] = (float)((int)((rng>>16)&0xFF) - 128) * 0.0078125f;
            }
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            TMP[(rng>>8)&31] = ((rng & 0x10000u) ? 10.0f : -10.0f);

            qblock(BQ, TMP);
            dqblock(BACK, BQ);
            for(i=0;i<32;i++) e_plain = e_plain + (BACK[i]-TMP[i])*(BACK[i]-TMP[i]);

            __vsetlen(32);
            __vhad32(RX, TMP, 0u);
            __mxqrot(BQ2, TMP, 0u);
            dqblock(RW, BQ2);
            for(i=0;i<32;i++) e_rot = e_rot + (RW[i]-RX[i])*(RW[i]-RX[i]);
        }
    }
    ok("outlier-improves", e_rot < e_plain);
    us("\n  err over "); ud(NTRIAL); us(" outlier blocks: plain="); uh(f2b(e_plain));
    us(" rotated="); uh(f2b(e_rot));

    us("\nchecks="); ud((unsigned)ck); us(" failures="); ud((unsigned)bad);
    us(bad?"\nBRQ: FAIL\n":"\nBRQ: PASS\n");
    return bad?1:0;
}
