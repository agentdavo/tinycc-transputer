typedef unsigned u32;
struct pair { u32 lo, hi; };
extern void low64(const void*,void*),low32(const void*,void*),high64(const void*,void*),high32(const void*,void*),nonround(const void*,void*),tcwg_down(const void*,void*);
static const struct pair in[] = {
 {0,0},{1,0},{0xffffffffu,0xffffffffu},{0xffffffffu,0},{0,1},{1,1},
 {0xffffffffu,0xfffffffeu},{0,0xffffffffu},{1,0xffffffffu},
 {0xffffffffu,0x7fffffffu},{0,0x80000000u},{1,0x10000010u}
};
static const struct pair db[] = {{0,0},{0,0x80000000u},{0,0x3ff00000},{1,0x3ff00000},{0,0x38100000},{0xffffffffu,0x380fffffu},{0,0x38000000},{0xffffffffu,0x47efffff}};
static void c(int x) { *(volatile u32*)0x10000000u=(unsigned char)x; }
static void h(u32 n) { int j; for(j=28;j>=0;j-=4)c("0123456789abcdef"[(n>>j)&15]); }
static void pair(struct pair *p) {h(p->hi); h(p->lo);}
void test_main(void) {
 u32 i,a,b,f; struct pair d,e;
 for(i=0;i<sizeof(in)/sizeof(in[0]);i++) {
  low64(&in[i],&d); low32(&in[i],&a); high64(&in[i],&e); high32(&in[i],&b); tcwg_down(&in[i],&f);
  c('I');h(i);c(' ');pair(&d);c(' ');h(a);c(' ');pair(&e);c(' ');h(b);c(' ');h(f);c('\n');
 }
 for(i=0;i<sizeof(db)/sizeof(db[0]);i++) { nonround(&db[i],&a);c('N');h(i);c(' ');h(a);c('\n'); }
}
