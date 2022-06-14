struct object {
    ORC_TEST_VIRTUAL int api() const { return 42; }
};

// Required so the compiler generates a symbol.
int (object::*api_pointer)() const  = &object::api;
