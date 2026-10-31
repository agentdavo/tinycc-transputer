// Simple test for logical operations
int test_logical_simple(void) {
    int a = 1;
    int b = 0;

    // Test AND
    if (a && b) {
        return 1;
    }

    // Test OR
    if (a || b) {
        return 2;
    }

    return 0;
}