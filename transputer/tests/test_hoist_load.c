/* Expected: SHOISTLOADOK\nH */
/*
 * test_hoist_load.c - tp_hoist moving a loop-invariant load out of a loop
 * (TCC_TRANSPUTER_HOIST_LOAD, transputer-fnopt.h): only when the loop
 * stores nothing, calls nothing, names no volatile, and the same word was
 * already read on the straight way into the loop.
 *
 *   find      core_list_find's shape: info->idx read before the loop and in
 *             every trip; hoisted
 *   find_wr   the loop writes through a pointer that is the key's own
 *             object: never hoisted, so it sees the new key
 *   find_call the loop calls a function that changes the key
 *   find_vol  the key is volatile
 *   find_cold no read of the key before the loop: not hoisted (a list of
 *             none must not read it at all; here it reads fine either way,
 *             so the check is that the result is right)
 */

#define UART_DATA (*(volatile unsigned int *)0x10000000)
static void up(int c) { UART_DATA = (unsigned)(unsigned char)c; }
static void us(const char *s) { while (*s) up(*s++); }

struct info { short data16; short idx; };
struct node { struct node *next; struct info *info; };

static struct node *find(struct node *list, struct info *info)
{
    if (info->idx >= 0) {
        while (list && list->info->idx != info->idx)
            list = list->next;
        return list;
    }
    return 0;
}

static struct node *find_wr(struct node *list, struct info *info)
{
    if (info->idx >= 0) {
        while (list && list->info->idx != info->idx) {
            if (list->next)             /* may be the key's own object */
                list->next->info->idx += 1;
            list = list->next;
        }
        return list;
    }
    return 0;
}

static struct info *victim;
static void bump(void) { victim->idx += 2; }

static struct node *find_call(struct node *list, struct info *info)
{
    if (info->idx >= 0) {
        while (list && list->info->idx != info->idx) {
            bump();
            list = list->next;
        }
        return list;
    }
    return 0;
}

static struct node *find_vol(struct node *list, volatile struct info *info)
{
    if (info->idx >= 0) {
        while (list && list->info->idx != info->idx)
            list = list->next;
        return list;
    }
    return 0;
}

static struct node *find_cold(struct node *list, struct info *info)
{
    while (list && list->info->idx != info->idx)
        list = list->next;
    return list;
}

static int bad;
static void chk(int ok) { if (!ok) bad++; }

int test_main(void)
{
    static struct info in[6] = { {1, 10}, {2, 11}, {3, 12}, {4, 13}, {5, 14}, {6, 15} };
    static struct node nd[6];
    struct info key, *k2;
    int i;

    for (i = 0; i < 6; i++) {
        nd[i].next = i < 5 ? &nd[i + 1] : 0;
        nd[i].info = &in[i];
    }
    key.data16 = 0;
    key.idx = 13; chk(find(nd, &key) == &nd[3]);
    key.idx = 10; chk(find(nd, &key) == &nd[0]);
    key.idx = 99; chk(find(nd, &key) == 0);
    chk(find(0, &key) == 0);

    /* the call moves the key by 2 each trip: 9 misses nd[0] (10), becomes
     * 11 and meets nd[1]; a key read once would miss everything */
    key.idx = 9;
    victim = &key;
    chk(find_call(nd, &key) == &nd[1] && key.idx == 11);

    key.idx = 12; chk(find_vol(nd, &key) == &nd[2]);
    key.idx = 15; chk(find_cold(nd, &key) == &nd[5]);
    chk(find_cold(0, &key) == 0);

    /* the key is in[1] (11): the first trip, missing nd[0], bumps in[1] to
     * 12 and nd[1] then matches; a key read once (11) would miss */
    k2 = &in[1];
    chk(find_wr(nd, k2) == &nd[1] && in[1].idx == 12);

    us(bad ? "HOISTLOADFAIL\n" : "HOISTLOADOK\n");
    return 0;
}
