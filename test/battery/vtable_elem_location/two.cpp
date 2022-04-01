struct object {
    virtual int api() const;
};

int two(const object& o) {
    return o.api();
}
