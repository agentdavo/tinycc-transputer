/* listfind-repro.c - the core_list_find shape that loop hoisting cannot
 * take (COREMARK-INSTR.md, "Handoff").  Compare:
 *   TP_NO_SCHED=1 transputer-tcc -mt9000 -c listfind-repro.c
 *   transputer-tcc -mt9000 -c listfind-repro.c
 * info->idx (`ldl info; adc 2; lsx`) does not change in the loop. */
struct d { short data16; short idx; };
struct l { struct l *next; struct d *info; };

struct l *find(struct l *list, const struct d *info)
{
    while (list && (list->info->idx != info->idx))
        list = list->next;
    return list;
}
