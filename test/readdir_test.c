#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>

int
main()
{
    int i;
    DIR* dir;
    struct dirent* dp;
    
    if (NULL == (dir = opendir("/home/matsuday/highway_jpg"))){
        printf("ディレクトリを開けませんでした\n");
        exit(1);
    }
    for(i = 0; NULL != (dp = readdir(dir)); i++){
        printf("%s\n" , dp->d_name);
    }
    closedir(dir);
    return 0;
}
