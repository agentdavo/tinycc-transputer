/* Code-generation fixture: volatile aggregates must retain per-word reads. */
struct plain { unsigned char bytes[17]; };
struct inner { volatile unsigned int value; };
struct nested { unsigned char prefix; struct inner member[2]; unsigned char tail; };
union variant { unsigned int plain; volatile unsigned int device; };
struct with_union { union variant u; unsigned int tail; };
struct pointer_to_volatile { volatile unsigned int *p; unsigned int data[3]; };
extern int take_plain(struct plain);
extern int take_nested(struct nested);
extern int take_union(struct with_union);
extern int take_pointer(struct pointer_to_volatile);
volatile struct plain global_volatile;
int from_volatile_global(void) { return take_plain(global_volatile); }
int from_volatile_pointer(volatile struct plain *p) { return take_plain(*p); }
int from_plain_pointer(struct plain *p) { return take_plain(*p); }
int from_volatile_member(struct nested *p) { return take_nested(*p); }
int from_volatile_union(struct with_union *p) { return take_union(*p); }
int from_pointer_to_volatile(struct pointer_to_volatile *p) { return take_pointer(*p); }
