void lock_test_bug(void);
#define BUG_ON(c) do { if (c) lock_test_bug(); } while (0)
