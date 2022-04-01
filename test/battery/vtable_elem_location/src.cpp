struct object {
#if OPTIONAL_API()
    virtual int optional() const;
#endif //  OPTIONAL_API()
    virtual int api() const;
};

static object instance;
