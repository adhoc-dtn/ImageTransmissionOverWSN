#include <stdio.h>
#include <string.h>
#define MAXITEM 20

int split( char *str, const char *delim, char *outlist[] ) {
    char    *tk;
    int     cnt = 0;

    tk = strtok( str, delim );
    while( tk != NULL && cnt < MAXITEM ) {
        outlist[cnt++] = tk;
        tk = strtok( NULL, delim );
    }
    return cnt;
}

main()
{
    char    memostr[] = "Milk, Eggs:Bread, Apple, Cheese";
    char    *shoppinglst[MAXITEM];
    int     i, cnt;

    cnt = split( memostr, ", :" , shoppinglst );
    printf( "memostr = '%s'\n", memostr );
    for( i = 0; i < cnt; i++ ) {
        printf( "%d:%s\n", i+1, shoppinglst[i] );
    }
}
