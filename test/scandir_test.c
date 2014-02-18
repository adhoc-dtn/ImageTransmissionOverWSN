#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

#include <fcntl.h>


/*
 * グローバルとか使いたくないんだけど、ローカルから二次元配列渡して
 * 要素コピーするとメモリアクセス違反になるので定義 10kbytes * 10
 */
#define NUMLINES 10
#define NUMBYTES 100000
/*1ストリームの上限10kbytes*/
char outlist[NUMLINES][NUMBYTES];

/*ファイル大きさ確認*/
int checkFileSize(char *FileName){
  FILE* fp;
  int i;

  fp=fopen(FileName,"r");
  //  printf("filename %s filediscriptor: %#x\n",FileName, fp);
  if(fp == NULL){
      printf("file open error : %s\n",FileName);
      exit(EXIT_FAILURE);
  }
  fseek(fp,0,SEEK_END);
  i=ftell(fp);
  fclose(fp);
  return i;
 
}


int scanDirFilter(const struct dirent * dire){

    //Discard . and ..
    if( strncmp(dire->d_name, ".", 2) == 0
        || strncmp(dire->d_name, "..", 3) == 0 )
        return 0;

    return 1;
}

/*s
 * pStr中のpSearchまでパターンマッチし、
 * マッチした部分までの文字列をoutputに代入 これを繰り返す
 * (最後尾にマッチしたものはカウントしない)
 * 
 * 返り値:パターンマッチありの場合
 * endStream.row=outputの行数, endStream.laststream=(最後のpSearchの次のストリームの先頭ポインタ)
 *      :マッチありだが終端  endStream.laststream=0, endStream.laststream=先頭ポインタ
 *      :パターンマッチなしの場合  endStream.laststream=-1, endStream.laststream=NULL;
 */

struct splitBitStream {
    int row;
    char *laststream;
};

struct splitBitStream
my_splitpattern(char *pStr,    int strLen,
                char *pSearch, int searchLen)
{
    int i, j, row, rank, match;
    struct splitBitStream endStream;
    
    row = rank = 0;
    match = -1; //マッチしない場合:-1
    if (strLen < searchLen) {
        endStream.row         = -1;
        endStream.laststream  = NULL;
        return endStream;
    }
    
    for ( i = 0; i < strLen - searchLen ; i++) {
        //[ i < strlen-searchLen+1 から変更] 終端までマッチしないようにする 
        if ( memcmp(&pStr[i], pSearch, searchLen) == 0 ) {
            //return &pStr[i]
            printf("in memcmp there is 0xffd9\n");
            for (j=0; j<searchLen; j++) {
                outlist[row][rank++] = pStr[i+j];
                printf("%#x in memcmp\n at i=%d\n",(int)pStr[i+j],i);
            }
            endStream.laststream  = &pStr[i+j]; //last格納
            i += searchLen-1;
            //outlist[row][rank] = '\0'; //終端への\0記号(対象がビットストリームの場合、意味ない)
            row++;
            rank = 0;
            match=1;
        }else {
            outlist[row][rank++] = pStr[i];
        }
    }
    if ( memcmp(&pStr[i], pSearch, searchLen) == 0 ) { //終端がマッチ
        endStream.laststream = pStr;
        match = 1;
    }

    if (match) {
        endStream.row = row;
        return endStream;
    } else {
        endStream.row = -1;
        endStream.laststream = NULL;
        return endStream;
    }
}


char *my_memmem(char *pStr,    int strLen,
                char *pSearch, int searchLen)
{
    // 単純検索 世の中にはBM法とかKMV法など
    //色々と検索するロジックが存在します。高速化をしたいのであれば検討のこと。
    int i;

    if (strLen < searchLen) return NULL;

    for ( i = 0; i < strLen - searchLen + 1; i++) 
        if ( memcmp(&pStr[i], pSearch, searchLen) == 0 ) 
            return &pStr[i];
        
    
    return NULL;
}




int
main (int argc, char *argv[])
{
	int	i, j;

	const char *dirname = "/home/matsuday/highway_jpg/";
	struct dirent **namelist;
    struct stat state_of_file;

    char dir_and_filename[256];
    char *output_dir =  "/home/matsuday/highway_jpg/";
    char output_filename[256];
    int in, out;
    char s_image[NUMBYTES];
    
    
    char sequence_of_outputfiles[50];
	int r = scandir(dirname, &namelist, scanDirFilter, alphasort);
    unsigned short endofjpegfile = htons(0xffd9);//(2byte) 0xffd9の意味(バイトオーダの問題で逆)
    short  twobyte; 
    int filesize;
    int count = 0;

    struct splitBitStream statBitStream;

    
    if(r == -1) {
		err(EXIT_FAILURE, "%s", dirname);
	}

    
    count = 1;
    //hoge
    out = open("binary.jpg", O_WRONLY | O_CREAT, S_IREAD | S_IWRITE);
	for (i = 0; i < r; ++i) {

        //ファイル読み込み
        strcpy(dir_and_filename, dirname);
        strcat(dir_and_filename, namelist[i]->d_name);
        stat  (dir_and_filename, &state_of_file);
        free  (namelist[i]);
        //        filesize = state_of_file.st_size;
        filesize = checkFileSize(dir_and_filename);
        //        printf("name: %s(%p), volume: %d, in %#x, out %#x\n"
        //,dir_and_filename, dir_and_filename, filesize, in, out);
        in = open(dir_and_filename, O_RDONLY);

        if(filesize >= NUMBYTES) {
            printf("read ; size of buffer oberflow\n");
            exit(EXIT_FAILURE);
        } else {
            if(read(in, s_image, filesize)<0) {
                printf("file read error  %s\n",dir_and_filename);
                exit(EXIT_FAILURE);
            }
            if(close(in)<0) {
                printf("close failed: %d\n",in); 
            }
        }
       

        //hoge
        write (out, s_image, state_of_file.st_size);
        
        /*
        //画像書き込み
         //strcpy (output_filename, output_dir);
         strcpy (output_filename, "output/output");
         sprintf(sequence_of_outputfiles, "%05d", count);
         strcat (output_filename, sequence_of_outputfiles);
         strcat (output_filename, ".jpg");
         out = open(output_filename, O_WRONLY | O_CREAT, S_IREAD | S_IWRITE);
         //グローバル変数初期化
         for (j=0; j<NUMLINES; j++) {
             memset(outlist[j], '\0', NUMBYTE);
         }

         statBitStream=my_splitpattern(s_image,
                                       filesize,
                                       &endofjpegfile,
                                       sizeof(short));
         
         if(statBitStream.row>=0) { //0xffd9のマッチ

             printf("row = %d, s_image = %p, laststream = %p, sizeof laststream = %d\n"
                    ,statBitStream.row
                    ,s_image
                    ,statBitStream.laststream
                    ,abs(s_image+sizeof(char)*filesize-statBitStream.laststream));
             
             for (j=0; j<statBitStream.row; j++) {
                 //s_imageで最後以外の0xffd9のあるストリームをファイルに書き込み
                 write (out, outlist[j], sizeof(outlist[j]));
                 // sizeof(outlist[j]は実メモリ分になるが終端記号まで書き込めば
                 // jpgファイルは問題なく開けるはずなので問題ない？
                 if (close (out)<0) {
                     printf("close filed out %d\n", out);
                 }
                 printf(" %s created at for loop\n",output_filename);

                 strcpy(output_filename, "output/output");
                 count++; //number of pictures.
                 sprintf(sequence_of_outputfiles, "%05d", count); 
                 strcat (output_filename, sequence_of_outputfiles);
                 strcat (output_filename, ".jpg");
                 out = open(output_filename, O_WRONLY | O_CREAT, S_IREAD | S_IWRITE);
             }

             write(out
                   ,statBitStream.laststream
                   ,abs(s_image+sizeof(char)*filesize-statBitStream.laststream));
          
             if ( my_memmem (statBitStream.laststream,
                             abs(s_image+sizeof(char)*filesize-statBitStream.laststream),
                             &endofjpegfile,
                             sizeof(short)) != NULL) {
                 //最後のストリーム中に0xffd9
                 
                 if(close(out)<0) {
                     printf("close is failed out\n");
                     exit(EXIT_FAILURE);
                 }
                 printf(" %s(%p) created at if memmem\n",output_filename,output_filename);
                 strcpy(output_filename, "output/output");
                 count ++; //number of pictures.
                 sprintf(sequence_of_outputfiles, "%05d", count);
                 strcat (output_filename, sequence_of_outputfiles);
                 strcat (output_filename, ".jpg");
                 out = open(output_filename, O_WRONLY | O_CREAT, S_IREAD | S_IWRITE);
             } //最後のストリーム中に0xffd9がない場合、クローズしない
               
             
         }else { //0xffd9のマッチなし
             write(out, s_image, filesize);
         }
        */
         
    }
    close(out);
    
	free(namelist);
    printf("all pictures = %d\n",count);

	exit (EXIT_SUCCESS);
}
