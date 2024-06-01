typedef struct {int _i;} S;

// Required so the compiler generates a symbol.
int geti(S s) { return s._i; }
