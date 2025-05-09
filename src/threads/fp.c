#include "threads/fp.h"
#include <stdint.h>
#include <round.h>

int add(int x, int y) {
    return x + y;
}

int sub(int x, int y) {
    return x - y;
}

int mult(int x, int y) {
    return ((int64_t) x) * y / f;
}

int div(int x, int y) {
    return ((int64_t) x) * f / y;
}

int int2fp(int n) {
    return n * f;
}

int fp2int(int x) {
    return x / f;
}