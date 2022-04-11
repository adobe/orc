struct object {
    ORC_TEST_VIRTUAL int api() const { return 42; }
};

int invoke(const object& o) {
    return o.api();
}
