typedef struct S {int _i;} S;

// Required so the compiler generates a symbol.
int get(S s) { return s._i; }
