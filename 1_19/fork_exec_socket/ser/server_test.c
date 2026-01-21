#include "server_function.h"
#include <stdio.h>
#include <stdlib.h>

// 의도적인 크래시 함수들
void test_segfault(void)
{
    int *p = NULL;
    *p = 42;  // SIGSEGV 발생 - NULL 포인터 역참조
}

void test_abort(void)
{
    abort();  // SIGABRT 발생 - 프로그램이 스스로 비정상 종료를 선언
}

void test_division_by_zero(void)
{
    int x = 1;
    int y = 0;
    int z = x / y;  // SIGFPE 발생
    printf("%d\n", z);
}

// 깊은 호출 스택 만들기
void deep_function_3(void)
{
    test_segfault();
}

void deep_function_2(void)
{
    deep_function_3();
}

void deep_function_1(void)
{
    deep_function_2();
}

void test_crash_with_stack(void)
{
    deep_function_1();
}