struct object {
    int pub() const { return api(); }
ORC_TEST_ACCESS:
    int api() const { return 42; }
};

#define XSTRCAT(x, y) x##y
#define STRCAT(x, y) XSTRCAT(x, y)
#define API_NAME STRCAT(api_, ORC_TEST_ACCESS)

int API_NAME(const object& o) {
    return o.pub();
}
