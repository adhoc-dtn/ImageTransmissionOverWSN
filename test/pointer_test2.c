#include <stdio.h>

int main()
{
    char hoge  = 'a';
    char hoge2 = 'b'; 
    char *p1 = &hoge;
    char *p2 = p1;
    char *p3 = p2;

    printf("first p1 is %p, p2 is %p p3 is %p\n",p1, p2, p3);
    
    p1 = &hoge2;
    printf("second p1 is %p, p2 is %p, p3 is %p\n",p1, p2, p3);
    p2 = p1;
    printf("third p1 is %p, p2 is %p, p3 is %p\n",p1, p2, p3);
        
    return 0;
}

