static int strlen(const char* p) {
    int n = 0;
    while (*p++) {
        ++n;
    }
    return n;
}

static int foo() {
#if ORC_TEST_FILE == 1
    return 42;
#else
    return strlen("Hello, world!");
#endif
}

// Required so the compiler generates a symbol.
int (*api_pointer)()  = &foo;
