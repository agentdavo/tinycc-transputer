/* t9000.h - T9000 virtual channels, the virtual channel processor (VCP),
 * the configuration space, semaphores and resources, through
 * transputer-tcc builtins (all of them need -mt9000 or -mt9000tvx).
 *
 * Sources: T9000 ISM ch.8 (semaphores, resources), ch.12 (channels, VLCBs,
 * instructions) and HRM ch.11 (VCP registers, packets), ch.15
 * (configuration register map).
 *
 * A virtual link is a pair of virtual channels multiplexed over one physical
 * link.  In order: t9_vcp_channels(n), t9_link_virtual() for each physical
 * link used, t9_vlink() for each virtual link (a 32-byte packet buffer and a
 * header of up to 3 bytes), t9_vcp_start(n).  Then use the channels with
 * in/out (__in/__out, ChanIn, ChanOut) or the variable-length __vin/__vout.  The header is what the far
 * end (or a C104 routing switch) sees: with 1-byte headers on a direct link,
 * header n names the receiver's virtual link n. */
#ifndef _T9000_H
#define _T9000_H

/* channel words (ISM fig 12.5) */
#define T9_EVENTIN(n)   ((void *)(0x80000020u + 8u * (n)))
#define T9_EVENTOUT(n)  ((void *)(0x80000024u + 8u * (n)))
#define T9_VIN(n)       ((void *)(0x80000040u + 8u * (n)))
#define T9_VOUT(n)      ((void *)(0x80000044u + 8u * (n)))

/* configuration registers (HRM ch.15) */
#define T9_CR_MININVALIDCHAN    0x010Fu
#define T9_CR_VCPSTATUS         0x0802u
#define T9_CR_VCPCOMMAND        0x0804u
#define T9_CR_VCPLINKMODE(l)    (0x0805u + 4u * (l))
#define T9_CR_VCPLINKMAXHDR(l)  (0x0806u + 4u * (l))
#define T9_CR_VCPLINKMINHDR(l)  (0x0807u + 4u * (l))
#define T9_CR_VCPLINKHDROFF(l)  (0x0808u + 4u * (l))
#define T9_CR_HDRAREABASE       0x0901u
#define T9_CR_DEVICEID          0x1001u
#define T9_CR_MEMSTART          0x410Eu
#define T9_CR_EXTERNALRCBASE    0x4110u

/* VCPCommand (HRM table 11.7) */
#define T9_VCP_RESET            1u
#define T9_VCP_START            2u
#define T9_VCP_STOP             4u
/* VCPLinkNMode: links reset into byte-stream (T800) mode */
#define T9_VLM_BYTEMODE         1u
#define T9_VLM_LOCALIZEERROR    2u
#define T9_VLM_HEADER2          4u      /* 2-byte headers, else 1 */

/* Link0-3 DS-link registers (HRM tables 13.5-13.7) */
#define T9_CR_LINKMODE(l)       (0x8001u + 0x100u * (l))
#define T9_CR_LINKCOMMAND(l)    (0x8002u + 0x100u * (l))       /* write only */
#define T9_CR_LINKSTATUS(l)     (0x8003u + 0x100u * (l))       /* read only */
#define T9_LM_LOCALIZEERROR     8u      /* errors reset the link, unreported */
#define T9_LC_RESETLINK         1u      /* reset the link engine, empty buffers */
#define T9_LC_STARTLINK         2u
#define T9_LC_RESETOUTPUT       4u      /* outputs low: a disconnect, far end */
#define T9_LC_WRONGPARITY       8u      /* a parity error at the far end */
#define T9_LS_LINKERROR         1u
#define T9_LS_LINKSTARTED       2u
#define T9_LS_RESETOUTPUT       4u      /* ResetOutput complete */
#define T9_LS_PARITYERROR       8u
#define T9_LS_DISCERROR         0x10u
#define T9_LS_TOKENRECEIVED     0x20u

#define T9_NULOFFSET            0xFFFFFFFFu     /* header kept in the VLCB */
#define T9_PACKET_BYTES         32              /* a packet buffer */

unsigned __ldconf(unsigned addr);
void     __stconf(unsigned addr, unsigned value);
void     __initvlcb(void *vl, void *pktbuf);
void     __sethdr(void *vl, unsigned link, unsigned hdroffset);
void     __writehdr(void *vl, unsigned len, const void *hdr);
void     __readhdr(void *vl, unsigned len, void *dst);
void     __setchmode(void *chan, unsigned active);
void     __stopch(void *chan);
char    *__swapbfr(void *vl, void *newbuf);
void     __vout(unsigned len, void *chan, const void *src);
unsigned __vin(unsigned max, void *chan, void *dst);    /* bytes received */
unsigned __ldchstatus(void *chan);
unsigned __chantype(void *chan);                        /* 1: internal */

/* VXFER (a TVX extension, not the T9000): start n transfers on virtual
 * channels at once and return when all are done - one instruction and one
 * deschedule where PAR needs a process each.  An input channel takes up to
 * len bytes into buf and len becomes the count received (T9_LENGTHERROR if
 * it overran).  An output channel sends len bytes from buf and is done once
 * its last packet has left: the VCP takes the final acknowledge itself, and
 * the link's next message waits for it.  Privileged, like __vin/__vout. */
typedef struct { void *chan; void *buf; unsigned len; } t9_xfer;
#define T9_LENGTHERROR          0xFFFFFFFFu     /* LengthError.p */
void     __vxfer(t9_xfer *list, unsigned n);

/* First: make virtual links 0..n-1 legal channel addresses. */
static void t9_vcp_channels(int n)
{
    __stconf(T9_CR_MININVALIDCHAN, (unsigned)T9_VIN(n));
}

/* Virtual link vl on physical link `link`, with a short (1..3 byte) header;
 * pktbuf: T9_PACKET_BYTES, word aligned, private to the VCP from now on. */
static void t9_vlink(int vl, void *pktbuf, unsigned link,
                     const void *hdr, unsigned hdrlen)
{
    __initvlcb(T9_VIN(vl), pktbuf);
    __sethdr(T9_VIN(vl), link, T9_NULOFFSET);
    __writehdr(T9_VIN(vl), hdrlen, hdr);
}

/* Put a physical link in virtual mode with 1- or 2-byte headers. */
static void t9_link_virtual(unsigned link, int header2)
{
    __stconf(T9_CR_VCPLINKMODE(link), header2 ? T9_VLM_HEADER2 : 0u);
}

/* Start the VCP, then activate both channels of virtual links 0..n-1. */
static void t9_vcp_start(int n)
{
    int vl;
    __stconf(T9_CR_VCPCOMMAND, T9_VCP_START);
    for (vl = 0; vl < n; vl++) {
        __setchmode(T9_VIN(vl), 1);
        __setchmode(T9_VOUT(vl), 1);
    }
}

/* ---- semaphores (ISM 8.6) ------------------------------------------------
 * A count and a queue of waiting processes, in three words.  SemWait takes
 * a count, or deschedules until a SemSignal gives it one; SemSignal wakes
 * the first waiter, or adds a count.  One instruction each, so they are
 * safe under timeslicing, unlike anything built from T800 instructions (see
 * conc.h).  The names follow the INMOS C library's. */
typedef struct { int count; void *front, *back; } Semaphore;
#define SEMAPHOREINIT(n)  { (n), (void *)0x80000000u, (void *)0x80000000u }
void     __semwait(void *sem);
void     __semsignal(void *sem);

static inline void SemInit(Semaphore *s, int n)
{
    s->count = n;
    s->front = s->back = (void *)0x80000000u;
}
static inline void SemWait(Semaphore *s)   { __semwait(s); }
static inline void SemSignal(Semaphore *s) { __semsignal(s); }

/* ---- resources (ISM 8.8, 12.7) -------------------------------------------
 * A resource joins many users to one server.  Each user outputs on its own
 * resource channel exactly as on an ordinary one; the server grants the
 * users one at a time, in the order their outputs arrived, and inputs from
 * the one granted - so, unlike an ALT, it need not know or enable every
 * user's channel, and a grant costs the same however many users there are.
 *
 *   t9_rds      the resource data structure: the waiting server, and the
 *               queue of channels whose output has arrived
 *   t9_reschan  an internal resource channel: the channel word, rc.Ptr and
 *               rc.Id.  A virtual input channel, T9_VIN(n), can be a
 *               resource channel too; the VCP holds its two words.
 *
 * t9_mark(chan, rds, id) puts a channel in the resource.  A grant takes it
 * out again, so it has to be marked again before it can be granted again:
 * by the user before its next output, or by the server at any time.  A
 * virtual channel is marked on the server's node.  The id is what the grant
 * returns; it must not be NotProcess, which rc.Id holds while a channel is
 * not in a resource.
 *
 * A grant also leaves rc.Ptr at NotProcess, so after it t9_reschan.reply is
 * an ordinary channel the server can answer on (for a virtual channel, use
 * the virtual link's output channel).
 *
 * The VCP keeps an event or virtual input channel's rc.Ptr and rc.Id in
 * guest memory at ExternalRCBase, 8 bytes per channel pair counted from the
 * first event channel, so before marking any of those give it an area with
 * t9_vcp_resources(area, n) - T9_EXTRC_BYTES(n) bytes, word aligned, for
 * virtual links 0..n-1 - which also sets every rc.Id to NotProcess. */
typedef struct { unsigned proc, front, back; } t9_rds;
#define T9_RDS_INIT      { 0x80000000u, 0x80000000u, 0x80000000u }
typedef struct { volatile int chan, reply; unsigned id; } t9_reschan;
#define T9_RESCHAN_INIT  { (int)0x80000000, (int)0x80000000, 0x80000000u }

void     __mkrc(void *chan, void *rds, unsigned id);
void     __unmkrc(void *chan);
void     __grant(void *rds, unsigned *id);              /* may deschedule */
void     __enbg(int guard, void *rds);                  /* ALT, as __enbc */
int      __disg(int code, int guard, void *rds);        /* ALT, as __disc */
void     __irdsq(void *rds, void *front, void *back);   /* queue to front */
void    *__erdsq(void *rds, void **back);               /* take the queue */
unsigned __ldresptr(void *chan);                        /* rc.Ptr */
void     __stresptr(void *chan, unsigned value);

#define T9_EXTRC_BYTES(n)   (32u + 8u * (unsigned)(n))
static inline void t9_vcp_resources(void *area, int n)
{
    unsigned *w = (unsigned *)area;
    unsigned i;

    for (i = 0; i < T9_EXTRC_BYTES(n) / 4u; i++)
        w[i] = 0x80000000u;
    __stconf(T9_CR_EXTERNALRCBASE, (unsigned)area);
}

static inline void t9_rds_init(t9_rds *r)
{
    r->proc = r->front = r->back = 0x80000000u;
}
static inline void t9_reschan_init(t9_reschan *c)
{
    c->chan = c->reply = (int)0x80000000;
    c->id = 0x80000000u;
}
static inline void t9_mark(void *chan, t9_rds *r, unsigned id)
{
    __mkrc(chan, r, id);
}
static inline void t9_unmark(void *chan) { __unmkrc(chan); }

/* The id of the next user, descheduling until one has output. */
static inline unsigned t9_grant(t9_rds *r)
{
    unsigned id;
    __grant(r, &id);
    return id;
}

/* An ALT over nc channels and nr resources: wait until a channel has an
 * output waiting or a resource has a channel queued, and return its index,
 * channels 0..nc-1 then resources nc..nc+nr-1 (also the priority order).
 * A resource that wins still has to be granted; t9_grant then returns at
 * once.  The ALT sequence must run in one function whose first parameter
 * the hardware overwrites (see lib/transputer-conc.c), hence slot0. */
static inline int t9_alt_core(int slot0, volatile int **chans, int nc,
                              t9_rds **rds, int nr)
{
    int i, sel = -1;

    (void)slot0;
    __alt();
    for (i = 0; i < nc; i++)
        __enbc(1, chans[i]);
    for (i = 0; i < nr; i++)
        __enbg(1, rds[i]);
    __altwt();
    for (i = 0; i < nc; i++)
        if (__disc(i, 1, chans[i]))
            sel = i;
    for (i = 0; i < nr; i++)
        if (__disg(nc + i, 1, rds[i]))
            sel = nc + i;
    return sel;
}
static inline int t9_alt(volatile int **chans, int nc, t9_rds **rds, int nr)
{
    return t9_alt_core(0, chans, nc, rds, nr);
}

#endif
