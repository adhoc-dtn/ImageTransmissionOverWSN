/* header files */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char outlist[100][5000];

int my_splitpattern(char *pStr,    int strLen,
                    char *pSearch, int searchLen)
{
    // 単純検索 世の中にはBM法とかKMV法など
    //色々と検索するロジックが存在します。高速化をしたいのであれば検討のこと。
    int i, j, row, rank;

    row = rank = 0;

    if (strLen < searchLen) return -1;

    for ( i = 0; i < strLen - searchLen + 1; i++) {
        if ( memcmp(&pStr[i], pSearch, searchLen) == 0 ) {
            //return &pStr[i]
            for (j=0; j<searchLen; j++) {
                outlist[row][rank++] = pStr[i+j];
            }
            i+=searchLen;
            outlist[row][rank] = '\0';
            row++;
            rank = 0;
        }else {
            outlist[row][rank++] = pStr[i];
        }
    }
    
    for (j=0; j<searchLen; j++) {
        outlist[row][rank++] = pStr[i+j];
    }
    outlist[row][rank] = '\0';
    row++;
    
    printf("in split pattern row: %d \n",row);
    return row;
}



/* main */
int main(void) {
    char s1[] = "i was born to love you. to born to hogehoge.";
    char cs1[20];
    char *split = "born"; 
    char *half,*residual;
    //    char outlist[100][5000];
    int i;
    int row;

    for (i=0; i<100; i++) {
        memset(outlist[i], '\0', 5000);
    }
       
    /* 1回目の呼出し */
    half          = strtok(cs1, split);
 
    if ((row = my_splitpattern(s1, strlen(s1), split, strlen(split))) <0 )
        {
            printf("split is fail.\n");
            exit(EXIT_FAILURE);
        }


    printf("row == %d\n",row);

    for (i=0; i<row; i++) {
        printf("%d: %s\n",i, outlist[i]);
    }
    return EXIT_SUCCESS;
}
