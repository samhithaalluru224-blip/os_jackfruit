#include <stdio.h>

int main() {
    volatile unsigned long long x = 0;

    while (1) {
        x++;
        if (x % 100000000 == 0) {
            printf("progress=%llu\n", x);
            fflush(stdout);
        }
    }
}
