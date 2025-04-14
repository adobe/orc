struct object {
    ORC_TEST_VIRTUAL int api() const { return 42; }
};

// Required so the compiler generates a symbol.

typedef int (object::*api_pointer)() const;

api_pointer foo() {
    return &object::api;
}
