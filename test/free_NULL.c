#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

int main()
{
    char *buffer;

    if ((buffer = (char *)malloc(sizeof(char)*10)) == NULL) {
        err(EXIT_FAILURE, "fail to allocate memory.\n");
    }
    strcpy(buffer, "i am sum");
    if(buffer == NULL) {
        printf("buffer is not allocate memory.\n");
    }else {
        printf("buffer is still active %s\n",buffer);
    }
    free(buffer);
    if(buffer == NULL) {
        printf("buffer is not allocate memory.\n");
    }else {
        printf("buffer is still active %s\n",buffer);
    }

    return 0;
}
