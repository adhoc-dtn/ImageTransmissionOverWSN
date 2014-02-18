#include <signal.h>  
#include <sys/types.h>  
#include <sys/stat.h>  
#include <stdio.h>  
#include <unistd.h>  
#include <string.h>  
#include <stdlib.h>  
  
unsigned char critical[100];  
void sigfunc(int signum)  
{  
    int i;  
    for ( i=0 ; i < sizeof(critical)/sizeof(critical[0]); ++i )  
    {  
        printf("%02X ", critical[i]);  
    }  
    printf("\n\n\n");  
}  
  
int main(int argc, char** arvg)  
{  
    int i;  
    struct sigaction sigact;  
    struct stat st;  
  
    memset(&sigact, 0, sizeof(sigact));  
    sigact.sa_handler = sigfunc;  
    sigemptyset(&sigact.sa_mask);  
    sigaddset(&sigact.sa_mask, SIGINT);  
    sigact.sa_flags = 0;  
    sigact.sa_restorer = NULL;  
  
    if ( 0 != sigaction(SIGINT, &sigact, NULL) )  
    {  
        fprintf(stderr, "error\n");  
        exit(1);  
    }  
  
    for(;;)  
    {  
        if ( stat("aaa", &st)==0 )  
            exit(0);  
  
        for ( i=0 ; i < sizeof(critical)/sizeof(critical[0]); ++i )  
        {  
            critical[i] = 0;  
        }  
        for ( i=0 ; i < sizeof(critical)/sizeof(critical[0]); ++i )  
        {  
            critical[i] = 0xff;  
        }  
    }  
  
    return 0;  
}  
