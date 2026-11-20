/* Only the unused matrix_print debug helpers need printf in a bare-metal BTL. */
int printf(const char *format, ...) { (void)format; return 0; }
