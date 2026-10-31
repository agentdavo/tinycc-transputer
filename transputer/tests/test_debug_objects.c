/*
 * Test file for debug_objects_early_init issues
 * Simplified version to identify TCC limitations
 */

// Bare-metal type definitions
typedef unsigned char uint8_t;
typedef unsigned int size_t;

// Simplified spinlock structure
struct simple_spinlock {
    int locked;
};

void simple_spin_lock_init(struct simple_spinlock *lock) {
    lock->locked = 0;
}

// Simplified hlist structures
struct hlist_node {
    struct hlist_node *next;
};

struct hlist_head {
    struct hlist_node *first;
};

void hlist_add_head(struct hlist_node *n, struct hlist_head *h) {
    n->next = h->first;
    h->first = n;
}

// Simplified debug object structures
#define ODEBUG_HASH_SIZE 16
#define ODEBUG_POOL_SIZE 64

struct debug_bucket {
    struct simple_spinlock lock;
};

struct debug_obj {
    struct hlist_node node;
};

static struct debug_bucket obj_hash[ODEBUG_HASH_SIZE];
static struct hlist_head obj_pool;
static struct debug_obj obj_static_pool[ODEBUG_POOL_SIZE];

// Test version of debug_objects_early_init
void test_debug_objects_early_init(void) {
    int i;

    // Initialize hash locks
    for (i = 0; i < ODEBUG_HASH_SIZE; i++) {
        simple_spin_lock_init(&obj_hash[i].lock);
    }

    // Add static objects to pool
    for (i = 0; i < ODEBUG_POOL_SIZE; i++) {
        hlist_add_head(&obj_static_pool[i].node, &obj_pool);
    }
}

// Test function that calls the problematic function
int test_debug_objects(void) {
    test_debug_objects_early_init();
    return 0;
}