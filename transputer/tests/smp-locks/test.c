#include <asm/spinlock.h>
#include <asm/cmpxchg.h>
#define R ((volatile unsigned int *)0x40000000u)
#define C ((volatile unsigned int *)0x41000000u)
static unsigned int irq_mask = 1;
unsigned long arch_local_irq_save(void) { unsigned int x=irq_mask; irq_mask=1; return x; }
int arch_local_irq_restore(unsigned long x) { irq_mask=x; return 0; }
void board_park(void);
void lock_test_bug(void) { const char *s="LOCK-BUG\n"; while (*s) *(volatile unsigned int *)0x10000000u=*s++; board_park(); }
int test_main(void)
{
    unsigned int id=C[6], i, seen, old, errors=0;
    arch_spinlock_t *spin=(arch_spinlock_t *)(R+0);
    arch_rwlock_t *rw=(arch_rwlock_t *)(R+1);
    volatile unsigned char *byte=(volatile unsigned char *)(R+8)+id;
    volatile unsigned short *half=(volatile unsigned short *)(R+10)+id;
    unsigned int local=123;
    if (arch_cmpxchg(&local,123,321)!=123 || local!=321) errors++;
    if (arch_xchg(&local,777)!=321 || local!=777) errors++;
    irq_mask = id & 1;
    R[16+id]=1;
    for (i=0;i<4;i++) while (!R[16+i]) {}
    for (i=0;i<1000;i++) {
        arch_spin_lock(spin); R[2]++; arch_spin_unlock(spin);
        arch_write_lock(rw); R[3]++; R[5]=R[3]; R[6]=~R[3]; arch_write_unlock(rw);
        arch_read_lock(rw); seen=R[3]; if (seen>4000 || (R[5]^R[6])!=~0u) errors++; arch_read_unlock(rw);
        do { old=R[4]; } while (arch_cmpxchg(R+4,old,old+1)!=old);
        if (arch_xchg(byte,(unsigned char)(i+1))!=(unsigned char)i) errors++;
        if (arch_xchg(half,(unsigned short)(i+1))!=(unsigned short)i) errors++;
    }
    if (irq_mask!=(id & 1)) errors++;
    R[32+id]=errors; R[48+id]=1;
    if (id) board_park();
    for (i=0;i<4;i++) while (!R[48+i]) {}
    for (i=0;i<4;i++) errors+=R[32+i];
    if (R[2]!=4000 || R[3]!=4000 || R[4]!=4000) errors++;
    if (R[8]!=0xe8e8e8e8u || R[10]!=0x03e803e8u || R[11]!=0x03e803e8u) errors++;
    { const char *s=errors ? "\nLOCK-FAIL\n" : "\nLOCK-PASS\n"; while(*s) *(volatile unsigned int *)0x10000000u=*s++; }
    return errors;
}
