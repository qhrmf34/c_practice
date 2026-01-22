#include "server_function.h"
void 
test_segfault(void)
{
    int *p = NULL;
    *p = 42;
}
void 
test_abort(void)
{
    abort();
}
void 
test_division_by_zero(void)
{
    int x = 1;
    int y = 0;
    int z = x / y;
    printf("%d\n", z);
}
void 
deep_function_3(void)
{
    test_segfault();
}
void 
deep_function_2(void)
{
    deep_function_3();
}
void 
deep_function_1(void)
{
    deep_function_2();
}
void 
test_crash_with_stack(void)
{
    deep_function_1();
}
