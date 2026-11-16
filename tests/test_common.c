#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "protocol.h"

int main(void)
{
    const char *ver = get_protocol_version_string();
    assert(ver != NULL);
    assert(strcmp(ver, "IT4062-V1") == 0);
    printf("All skeleton tests passed!\n");
    return 0;
}

