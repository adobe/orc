typedef struct { double _d; } S;

// Required so the compiler generates a symbol.
int getd(S s) { return s._d; }
