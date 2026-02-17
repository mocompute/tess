#include <stdio.h>
#include "mylib.h"

int main(void)
{
    tl_init_mylib();

    printf("addi(2, 3) = %d\n", MyLib_addi(2, 3));
    printf("addf(1.5, 2.5) = %f\n", MyLib_addf(1.5, 2.5));

    return 0;
}
