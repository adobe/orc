#include <iostream>

struct rect {
    int top{0};
    int bottom{0};
    int left{0};
    int right{0};

    inline auto height () const { return bottom - top; }
    inline auto width () const { return right - left; }
};

static inline auto area(const rect& r) {
    return r.height() * r.width();
}

int main() {
    int x;
    std::cin >> x;
    return area(rect{0, x, 0, x});
}