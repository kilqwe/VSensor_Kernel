#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int global_var = 42;

int main(void)
{
    int local_var = 10;
    int *heap_var;

    heap_var = malloc(sizeof(int));

    if (heap_var == NULL)
        return 1;

    *heap_var = 20;

    printf("global: %p\n", (void *)&global_var);
    printf("local:  %p\n", (void *)&local_var);
    printf("heap:   %p\n", (void *)heap_var);

    printf("pid: %d\n", getpid());

    free(heap_var);

    return 0;
}
