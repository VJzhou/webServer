//
// Created by vj-zhou on 2021/12/29.
//
#include <stdio.h>
int main()
{
    char name[50] = "zhouweijie123";
    char *p = NULL;
    p = name;
    printf("%s\n", p);

    p += 5;
    *p= '\0';
    printf("%s\n", name);
    printf("%c", *p);
    return 0;
}