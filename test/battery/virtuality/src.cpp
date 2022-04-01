struct object {
    int pub() const { return api(); }
ORC_TEST_ACCESS:
    int api() const { return 42; }
};

#define API_NAME api_##ORC_TEST_ACCESS

int API_NAME(const object& o) {
    return o.pub();
}
