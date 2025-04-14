struct object {
#if OPTIONAL_API()
    virtual int optional() const { return 42; }
#endif //  OPTIONAL_API()
    virtual int api() const { return 42; }
};

static object instance;
