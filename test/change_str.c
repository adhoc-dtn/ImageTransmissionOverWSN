#include <string.h>
#include <stdio.h>

int main()
{
    char *str   = "192.168.50.211";
    char *wlan  = "50";
    char *lan =  "30";
    
    printf("ip $s\n",str);
    strchg(str, wlan, lan);

    pritnf("ip $s\n",str);
    return 0;
    
}
