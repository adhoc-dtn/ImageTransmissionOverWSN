#include <stdio.h>
#include <stdlib.h>



struct temp {
    int one;
    int sec;
    int the;
};
struct hogehoge {
    int one;
    struct temp tmp;
};


int main() {
    struct hogehoge *p;
    
    p = (struct hogehoge *)malloc(sizeof (struct hogehoge));
    printf("sizeof *hogehoge == %d, sizeof hogehoge->one == %d, sizeof hogehoge->tmp == %d\n"
           ,sizeof(bnmp)
           ,sizeof(p->one)
           ,sizeof(p->tmp));
    return 0;
}
    
