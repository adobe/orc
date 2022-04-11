struct object {
    virtual ~object();

#if OPTIONAL_STUFF()
    bool _x;
#endif // OPTIONAL_STUFF()
    bool _y;
};

static object instance;
