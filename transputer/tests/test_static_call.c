/*
 * Test file for static_call_init issues
 * Simplified version to identify TCC limitations
 */

// Bare-metal type definitions
typedef unsigned char uint8_t;
typedef unsigned int size_t;

// Simple spinlock simulation
struct simple_spinlock {
    int locked;
};

void simple_spin_lock(struct simple_spinlock *lock) {
    while (lock->locked) {
        // Busy wait
    }
    lock->locked = 1;
}

void simple_spin_unlock(struct simple_spinlock *lock) {
    lock->locked = 0;
}

// Simple memory allocation simulation
void *simple_kzalloc(size_t size) {
    static char memory_pool[1024];
    static size_t used = 0;

    if (used + size > sizeof(memory_pool)) {
        return 0; // Out of memory
    }

    void *ptr = &memory_pool[used];
    used += size;

    // Zero the memory
    char *p = (char *)ptr;
    for (size_t i = 0; i < size; i++) {
        p[i] = 0;
    }

    return ptr;
}

// Simplified static call structures
struct static_call_site {
    void *addr;
};

struct static_call_key {
    int sites;
    int type;
    struct static_call_site *site_ptr;
};

// Simplified version of __static_call_init
int test_static_call_init(struct static_call_site *start,
                         struct static_call_site *stop) {
    struct static_call_site *site;
    struct static_call_key *key;
    int count = 0;

    if (start == stop)
        return 0;

    for (site = start; site < stop; site++) {
        // Simplified processing
        count++;
    }

    // Simulate allocation that might fail
    void *alloc_result = simple_kzalloc(64);
    if (!alloc_result) {
        return -1; // Allocation failure
    }

    return 0;
}

// Test function with locks and allocation
int test_static_call_with_locks(void) {
    static struct simple_spinlock test_lock = {0};

    // Test locking
    simple_spin_lock(&test_lock);

    // Test allocation within lock
    void *ptr = simple_kzalloc(32);
    if (!ptr) {
        simple_spin_unlock(&test_lock);
        return -1;
    }

    // Test array iteration (similar to static_call processing)
    struct static_call_site sites[3];
    int result = test_static_call_init(&sites[0], &sites[3]);

    simple_spin_unlock(&test_lock);

    return result;
}

// Test with inline functions
static inline int inline_func(int x) {
    return x * 2;
}

int test_inline_usage(void) {
    int val = 5;
    return inline_func(val); // This might be the TCC issue
}