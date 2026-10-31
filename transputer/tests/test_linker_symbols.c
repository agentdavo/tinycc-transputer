/*
 * Test file for linker-generated symbol issues
 * This is likely the root cause of TCC kernel problems
 */

// Bare-metal type definitions
typedef unsigned char uint8_t;
typedef unsigned int size_t;

// Simulate linker-generated symbols (these would be defined by linker script)
extern const void __start_test_section[];
extern const void __stop_test_section[];

// Test function that uses linker symbols
int test_linker_symbols(void) {
    // Calculate size of section using linker symbols
    size_t section_size = (size_t)((const char *)__stop_test_section - (const char *)__start_test_section);

    // Use the section
    if (section_size > 0) {
        const uint8_t *data = (const uint8_t *)__start_test_section;
        uint8_t checksum = 0;

        for (size_t i = 0; i < section_size; i++) {
            checksum ^= data[i];
        }

        return checksum;
    }

    return 0;
}

// Test with multiple linker symbols
extern const void __start_data_section[];
extern const void __stop_data_section[];

int test_multiple_linker_symbols(void) {
    size_t data_size = (size_t)((const char *)__stop_data_section - (const char *)__start_data_section);
    size_t test_size = (size_t)((const char *)__stop_test_section - (const char *)__start_test_section);

    return (int)(data_size + test_size);
}