struct object {
#if OPTIONAL_API()
    virtual int optional() const { return 0; }
#endif //  OPTIONAL_API()
    virtual int api() const { return 1; }
};

static object instance;
