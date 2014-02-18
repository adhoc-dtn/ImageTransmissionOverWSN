#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h> 
#include <fcntl.h> 
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <pthread.h>
#include <math.h>

#define BUFFER_SIZE 256



/*このあたり構造体化しておこ*/
pthread_t mainThread;
int numConnection=0;


struct socketManager{
    int dstSocket;
    struct sockaddr_in dstAddr;
    struct socketManager *next;
};
//ヘッド
struct socketManager sMHead;

//main では接続の受付を行う。
//
void *recvPacket (void *param) ;

int main () {

    struct socketManager *sM_new, *sM_prev;
    pthread_t threadID;
    struct sockaddr_in dstAddr;
    int dstAddrSize = sizeof(dstAddr);
   /* 送信元情報 */
    struct sockaddr_in srcAddr;
     /* ポート番号、ソケット */
    unsigned short port = 51000;
    int         True = 1;
    int srcSocket;  // 自分
    
    sM_prev = &sMHead;
    /* sockaddr_in 構造体のセット */
    memset(&srcAddr, 0, sizeof(srcAddr));
    srcAddr.sin_port = htons(port);
    srcAddr.sin_family = AF_INET;
    srcAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    /* ソケットの生成 */
    srcSocket = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(srcSocket, SOL_SOCKET, SO_REUSEADDR, &True, sizeof(int));

    /* ソケットのバインド */
    if (bind(srcSocket, (struct sockaddr *) &srcAddr, sizeof(srcAddr)) < 0) {
        err(EXIT_FAILURE, "bind fail\n");
    }
    
    /* 接続の受付け */
    printf("Waiting for connection ...\n");
    while(1) {
        sM_new = (struct socketManager *) malloc(sizeof(struct socketManager));
        sM_prev = &sMHead;
        sM_new->next = NULL;
        
        /* 接続の許可 */
        if (listen(srcSocket, 1) != 0) {
            err(EXIT_FAILURE, "fail to listen\n");
        }
        
        if ((sM_new->dstSocket = accept(srcSocket, (struct sockaddr *) &dstAddr, &dstAddrSize)) < 0) {
            err(EXIT_FAILURE, "fail to accept\n");
        }
        sM_new->dstAddr = dstAddr;
        /* if(numConnection == 0) { */
        /*     pthread_kill(mainThread, SIGUSR1); */
        /* } */
        if ( pthread_create (&threadID, NULL, recvPacket, sM_new) < 0) {
            err(EXIT_FAILURE, "error occur\n");
        }
        printf("Connected from %s create thread %#x(give %p)\n", inet_ntoa(dstAddr.sin_addr),threadID,sM_new);
        sM_prev = sM_new;
        
        numConnection++;
        
       
    }
    
}

void *recvPacket (void *param) {
   
    /* int srcSocket;  // 自分 */
    int dstSocket;  // 相手

    

    /* 各種パラメータ */
    int numrcv;
    int pacSize;
    char buffer[BUFFER_SIZE];
    int status;

    struct sockaddr_in clientAddr;
    unsigned int       clientAddrLen;  
    struct in_addr     clientIP;
    /* int val = 0; */
    
   /*受信タイムアウト用タイマー*/
    struct timeval server_timeout_timer;
    fd_set         fds, readfds;
    double         timer_int, timer_frac; 

    //初期化
    struct socketManager *sM_cur = (struct socketManager *)param;
    dstSocket = sM_cur->dstSocket;
  
    
    /* パケット受信 */
    /* ioctl(srcSocket, FIONBIO, &val); */
          
    memset(buffer, 0, sizeof(buffer));

    
     
    while(1) {
        /*タイマー値セット*/
        /* timer_frac                   = modf(3.5, &timer_int); */
        /* server_timeout_timer.tv_sec  = timer_int; */
        /* server_timeout_timer.tv_usec = 1000000*timer_frac; */
        
        /* FD_ZERO (&readfds); */
        /* FD_SET  (dstSocket, &readfds); */
        /* memcpy (&fds, &readfds, sizeof(fd_set)); */
        /* memset(buffer,  0, BUFFER_SIZE); */
        
        /* /\*---ソケットが読みだし可能になるまでwaitする---*\/ */
        /* if ( select (dstSocket+1, &fds, NULL, NULL, &server_timeout_timer) == 0) { */
        /*     /\* タイマー値経過までにソケットが読み込み可能にならなければプログラム終了 *\/ */
        /*     printf("[timer exceeds... exting.]\n"); */
        /*     close(dstSocket); */
                
        /*     exit(0); */
        /* } */
    
        /* /\*データ受信*\/ */
        /* if(FD_ISSET(dstSocket, &fds)) { */
            //受信パケットサイズ取得(ソケット内の受信キュー内からデータ削除しない)
        /* numrcv     = recvfrom(dstSocket */
        /*                       , buffer */
        /*                       , BUFFER_SIZE */
        /*                       , MSG_PEEK */
        /*                       ,(struct sockaddr *) &clientAddr */
        /*                       , &clientAddrLen); */
        /* //printf("recv imgDataValMess size %d\n",numrcv); */
        /* numrcv = recvfrom(dstSocket */
        /*                   ,buffer */
        /*                   ,BUFFER_SIZE */
        /*                   ,0 */
        /*                   ,(struct sockaddr *) &clientAddr */
        /*                   ,&clientAddrLen); */
        numrcv  = recv(dstSocket
                       , buffer
                       , BUFFER_SIZE
                       , MSG_PEEK);
        //printf("recv imgDataValMess size %d\n",numrcv);
        numrcv = recv(dstSocket
                      ,buffer
                      ,numrcv
                      ,0);

        if (numrcv <= 0) {
            close(dstSocket);
            printf("connection lost for  %s \n", inet_ntoa(sM_cur->dstAddr.sin_addr));
            return;
        }
                    
        printf("receivedmesssage [%s]  from  %s port %d (status %d)\n"
               ,buffer
               ,inet_ntoa(sM_cur->dstAddr.sin_addr)
               ,sM_cur->dstAddr.sin_port
               ,numrcv);
    }
    printf("recv done from \n");
    
}
