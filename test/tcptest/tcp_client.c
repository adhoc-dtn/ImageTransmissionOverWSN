#include <stdio.h>

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

struct in_addr GetMyIpAddr(char* device_name);

int
main(int argc, char *argv[]) {
  /* IP アドレス、ポート番号、ソケット */
  char destination[80];
  unsigned short port = 51000;
  int dstSocket;

  /* sockaddr_in 構造体 */
  struct sockaddr_in dstAddr;
  struct in_addr myIP;
  /* 各種パラメータ */
  int status;
  int numsnt;
  char toSendText[20];
  int i;
  sigset_t    signal_set;
  int         sig_received;
  int         signal_send_error = SIGPIPE;
  int         counter=0;
  int         second=0;

/* for get time */
  struct timeval current_time;
  int            day,usec;
  double time_before_send, time_after_send;
  double sendInterval;
  double    int_sec;
  double    int_usec;
  
  if(argc != 2) {
      printf("this program [interval of send]\n");
      exit(1);
  }

  sendInterval = (double)atof(argv[1]);
  int_usec   = modf(sendInterval,  &int_sec);

  /*シグナルの登録(queue) とブロック設定*/
  /* sigemptyset     ( &signal_set ); */
  /* sigaddset       ( &signal_set, signal_send_error );   // dequeue */
  /* pthread_sigmask ( SIG_BLOCK, &signal_set, 0 ); */
  /************************************************************/
  /* 相手先アドレスの入力 */
  printf("Connect to ? : (name or IP address) ");
  scanf("%s", destination);

  
 
  /* ソケット生成 */
  dstSocket = socket(AF_INET, SOCK_STREAM, 0);

  /* 接続 */
  while(1) {
      /* sockaddr_in 構造体のセット */
      memset(&dstAddr, 0, sizeof(dstAddr));
      dstAddr.sin_port = htons(port);
      dstAddr.sin_family = AF_INET;
      dstAddr.sin_addr.s_addr = inet_addr(destination);
      status = connect(dstSocket, (struct sockaddr *) &dstAddr, sizeof(dstAddr));
      /* if(second && status<0) { */
      /*     err(EXIT_FAILURE, "fail to connect\n"); */
      /* }*/
      printf("Trying to connect to %s: status %d (%d) \n", destination, status,counter++);

      /* コネクションが張れるまでconnect */
      if (status >= 0 ) {
          counter = 0;
          myIP = GetMyIpAddr("eth0");
          sprintf(toSendText, "message from %s",inet_ntoa(myIP));
          /* パケット送出 */
          while(1) {
              gettimeofday(&current_time, NULL);
              day   = (int)current_time.tv_sec;
              usec  = (int)current_time.tv_usec;
              time_before_send = day + (double)usec/1000000; //送信開始時間保存
              if ((numsnt = send(dstSocket, toSendText, strlen(toSendText)+1, MSG_NOSIGNAL)) < 0) {
                  gettimeofday(&current_time, NULL);
                  day   = (int)current_time.tv_sec;
                  usec  = (int)current_time.tv_usec;
                  time_after_send = day + (double)usec/1000000; //処理開始時間保存
                  printf("[send] takes %lf  sec for send.\n",time_after_send-time_before_send);
                  
                  /* close(dstSocket); */
                   if ((dstSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0) { 
                       err(EXIT_FAILURE, "fail to create socoket\n"); 
                   } 
                  second = 1;
                  break;
              }
              printf("sending...from %s %d bytes\n",inet_ntoa(myIP),numsnt);
              gettimeofday(&current_time, NULL);
              day   = (int)current_time.tv_sec;
              usec  = (int)current_time.tv_usec;
              time_after_send = day + (double)usec/1000000; //処理開始時間保存
              /* printf("[send] takes %lf sec for send.\n",time_after_send-time_before_send); */
                  
              sleep(int_sec);
              usleep(int_usec);
          }
      }
      
      sleep(1);
  }
  /* ソケット終了 */
}

/* 指定インタフェースに割り当てられているIPアドレスの取得 */
/**
 * @param  デバイス名(e.g. "eth0")
 * @return struct in_addr * ipv4アドレス                                                                            
 */
struct in_addr GetMyIpAddr(char* device_name) {
    int s = socket(AF_INET, SOCK_STREAM, 0);

    struct ifreq ifr;
    ifr.ifr_addr.sa_family = AF_INET;
    strcpy(ifr.ifr_name, device_name);
    ioctl(s, SIOCGIFADDR, &ifr);
    close(s);

    struct sockaddr_in addr;
    memcpy( &addr, &ifr.ifr_ifru.ifru_addr, sizeof(struct sockaddr_in) );
    return addr.sin_addr;
}


