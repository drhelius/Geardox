#include <stdio.h>
#include "gdxdbg.h"

static int squares[8];

static int square(int value)
{
    return value * value;
}

int main(void)
{
    int i;
    int total = 0;

    for (i = 0; i < 8; i++)
    {
        squares[i] = square(i);
        total += squares[i];
    }

    gdx_printf("total %d\n", total);
    printf("Hello from DJGPP, the squares add up to %d\n", total);
    return total == 140 ? 0 : 1;
}
