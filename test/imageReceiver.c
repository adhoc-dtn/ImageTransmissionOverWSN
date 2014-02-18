#include<stdio.h>
#include<sys/socket.h>
#include<sys/stat.h>
#include<arpa/inet.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<fcntl.h>   // O_RDONLY等
#include<netinet/in.h>
#include<errno.h>
#include <err.h> 
#include <sys/types.h> 
#include <dirent.h> 

#define DEBUG
#define MAXPENDING	50
#define BUF_LEN		512
#define FILENAME_LEN	100
#define DIRNAME_LEN	100
#define TIME            60

void DieWithError(char *errorMessage);
void HandleTCPClient(int clntSocket);

/*scandirで.から始まるファイルを除去する関数*/
static int isNotDotFile(const struct dirent *entry)
{
    return entry->d_name[0] != '.';
}

int main(int argc, char *argv[])
{
  int i=0,c=0,t=1,pid,in,next_in,out,n=-1,sw,n_bak,fs,total_fs=0,throughput=0;
  int servSock,clntSock,listenSock;
  const char *dirname ="";
  struct dirent **namelist;
  struct sockaddr_in echoServAddr;
  struct sockaddr_in echoClntAddr;
  unsigned short echoServPort;
  unsigned int clntLen;
  char jpeg_num[10]="1068";
  int jn=atoi(jpeg_num);
  char buf[BUF_LEN], buf_bak[BUF_LEN];
  char filesize[10];
  char ServName[20];
  char SendName[20];
  char  subServName[20]            ="A";
  char filename[FILENAME_LEN]	   = "";
  char next_filename[FILENAME_LEN] = "";
  char jpg[BUF_LEN]		   = ".jpg";
  char NULLPO[BUF_LEN]		   = "";
  char NULL_file[FILENAME_LEN]	   = "";
  char DIRNAME[DIRNAME_LEN]	   = "";

  if( argc > 4 || argc < 3)
    {
      fprintf(stderr, "Usage: %s <Server Port> <Directry> <HostName>\n",argv[0]);
      exit(1);
    }

  echoServPort = atoi(argv[1]);
  strcpy(DIRNAME,argv[2]);
  strcpy(ServName,argv[3]);
  printf("dir=%s\n",DIRNAME);
  printf("ServerName : %s\n",ServName);


  if((servSock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
      DieWithError("socket() failed");
  memset(&echoServAddr, 0, sizeof(echoServAddr));
  echoServAddr.sin_family      = AF_INET;
  echoServAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  echoServAddr.sin_port	       = htons(echoServPort);
  
  if(bind(servSock, (struct sockaddr *) &echoServAddr,sizeof(echoServAddr)) < 0)
    DieWithError("bind() failed");

  if((listenSock=listen(servSock, MAXPENDING)) < 0)
    DieWithError("listen() failed");
  
  clntLen = sizeof(echoClntAddr);
  if((clntSock = accept(servSock, (struct sockaddr *) &echoClntAddr, &clntLen)) < 0){
    DieWithError("accept() failed");
  }

  printf("Handring client %s\n",inet_ntoa(echoClntAddr.sin_addr));   

  //ここからコネクション維持ファイル転送ループ  
  for(;;){
    n=recv(clntSock,buf,BUF_LEN,0);/*ファイル名前送信要求を受ける*/
    write(1,buf,n);    /*"send file!:\n"を表示*/
    
    /*画像名を更新日時順に読み取る*/
    int r = scandir(DIRNAME, &namelist, isNotDotFile, alphasort);
    if(r == -1) { 
      err(EXIT_FAILURE, "%s", DIRNAME);
    } 

    /*サーバー起動時に画像が保存されていない時の処理*/
    if(r==0){
          printf("\nfile not exist\n");
	  while(r == 0){
r = scandir(DIRNAME, &namelist, isNotDotFile, alphasort);
 sleep(1);
	  }
    }


    strcat(ServName,jpeg_num);
    strcat(ServName,jpg);
    strcpy(SendName,ServName);
    strcpy(ServName,argv[3]);
    strcat(DIRNAME,namelist[i]->d_name);
    strcpy(filename,DIRNAME);
    strcpy(DIRNAME,argv[2]);



    
#ifdef DEBUG
    //scanf("%s",filename);
    printf("filename=%s\nsendname=%s\n",filename,SendName);
#endif /* DEBUG */
      
      send(clntSock,SendName,strlen(SendName)+1,0);
      /*送信ファイル名をクライアントに送信*/
      jn++;
      sprintf(jpeg_num,"%d",jn);
      
      n = recv(clntSock,buf,BUF_LEN,0);
      /*ファイルサイズ送信要求を受ける*/
      write(1,buf,n);/*"%s`s filesize=%s\n"を表示*/

     if(namelist[i+1] == NULL){
      printf("\nnext file not exist\n");
      for(;;){
	r = scandir(DIRNAME, &namelist, isNotDotFile, alphasort);
	if(namelist[i+1] != NULL){
	  break;
	}
	for(c=0;c<r;c++){
	  free(namelist[c]);
	}
	free(namelist);
	sleep(1);
      }
	  }


      /**************************/
      strcpy(ServName,argv[3]);


      strcat(ServName,jpeg_num);
      strcat(ServName,jpg);
      strcat(DIRNAME,namelist[i+1]->d_name);
      strcpy(next_filename,DIRNAME);
      strcpy(DIRNAME,argv[2]);
      strcpy(ServName,argv[3]);
      /*************************/

  printf("\nb\n");
	  // ファイル存在の確認
      for(;;){
	/*空ファイル送信を防ぐため、次番号jpgファイルが作られていなければ、現番号jpgファイルを開かない*/
	next_in = open(next_filename, O_RDONLY); 

#ifdef DEBUG
	printf("in=%d\n",in);
#endif /* DEBUG */

	if (next_in < 0)
	  {
	    if(t==1){
	      printf("***error*** %s doesn`t exist!! %s can`t send!!\n",next_filename,filename);
	    }
	    t++;
	  }else{
	  in = open(filename, O_RDONLY); 
	  
#ifdef DEBUG
	  printf("%s can send!!\n",filename);
#endif /* DEBUG */
	  
	  fs=FOO(filename);
	  sprintf(filesize,"%d",fs);
	  close(next_in);
	  
#ifdef DEBUG
	  printf("filesize_string=%s\n",filesize);
#endif /* DEBUG */
	  
	  t=1;
	  break;
	  
	} /* if */
      } /* for */

   for(c=0;c<r;c++){
    free(namelist[c]);
    }
    free(namelist);
    i++;

      send(clntSock,filesize,10,0);
      n = recv(clntSock,buf,BUF_LEN,0);
      write(1,buf,n);
      
      sw = 1;
      while (sw > 0) {
	n = read(in, buf, BUF_LEN);
	if (n > 0){
	  n = send(clntSock, buf, n,0);
	}else{
	  sw = 0;
	}
      }
      close(in);
      total_fs = fs + total_fs;
      throughput = total_fs/TIME;
      printf("[%s-1.jpg] send completed!!\n",jpeg_num);
      printf("TOTAL_FILESIZE = %d\nThroughput = %d\n",total_fs,throughput);
      //usleep(500000);
      //sleep(1);   
  }
  return 0;
}
