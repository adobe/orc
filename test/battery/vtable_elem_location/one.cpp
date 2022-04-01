struct object {
    virtual int optional() const;

    virtual int api() const;
};

int one(const object& o) {
    return o.api();
}
