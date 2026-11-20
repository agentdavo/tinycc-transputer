import sys
p = sys.argv[1]
s = open(p).read()
decl = r'''
/* T4_OPHIST=file: every instruction counted at its address (cm measurement build) */
#define OPH_BASE 0x80000000u
#define OPH_SIZE 0x00200000u
static int ophist_on;
static uint64_t *oph_cnt;
static uint16_t *oph_key;
static uint8_t *oph_len;
static void ophist_dump(void)
{
	const char *fn = getenv("T4_OPHIST");
	FILE *f;
	uint32_t i;
	if (!oph_cnt || !(f = fopen(fn, "w")))
		return;
	for (i = 0; i < OPH_SIZE; i++)
		if (oph_cnt[i])
			fprintf(f, "%08X %03X %u %llu 0 0\n", OPH_BASE + i,
				oph_key[i], oph_len[i], (unsigned long long)oph_cnt[i]);
	fclose(f);
}
static void ophist_init(void)
{
	ophist_on = getenv("T4_OPHIST") && *getenv("T4_OPHIST");
	if (!ophist_on)
		return;
	oph_cnt = calloc(OPH_SIZE, sizeof *oph_cnt);
	oph_key = calloc(OPH_SIZE, sizeof *oph_key);
	oph_len = calloc(OPH_SIZE, 1);
	if (!oph_cnt || !oph_key || !oph_len) { ophist_on = 0; return; }
	atexit(ophist_dump);
}

/* Next heartbeat_counter value at which run_insn_hooks() is due; 0 makes'''
a = "\n/* Next heartbeat_counter value at which run_insn_hooks() is due; 0 makes"
assert s.count(a) == 1
s = s.replace(a, decl, 1)
b = "\trare_hooks = !production_quiet || stop_iptr || dcache_enabled > 0 ||"
assert s.count(b) == 1
s = s.replace(b, "\tophist_init();\n" + b + "\n\t             ophist_on ||", 1)
c = "\t/* Feed the T9000 grouper model (T4_T9000=1); no cost when disabled. */"
assert s.count(c) == 1
hook = r'''	if (__builtin_expect(rare_hooks && ophist_on, 0)) {
		uint32_t ip = Icache[islot].ip, oa = ip - OPH_BASE;
		if (oa < OPH_SIZE) {
			oph_cnt[oa]++;
			oph_key[oa] = (uint16_t)(Icode == 0xF0 ? 0x100 + (OReg & 0x1FF) : Icode);
			oph_len[oa] = (uint8_t)(IPtr - ip);
		}
	}
'''
s = s.replace(c, hook + c, 1)
open(p, "w").write(s)
print("patched")
