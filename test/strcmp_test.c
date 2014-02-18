
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






/* ２つの文字列比較を行い、str1 eq str2なら0、それ以外は1を返す */
/* 注意 len(str1)<len(str2)は想定していない */
/* str1:比較対象 */
/* str2:探索パターン */
/* len : str2の文字列長 */
static int isSameStr(char *str1, char *str2, int len) {

    int i, sameNum = 0;
    int isHit = 0;
    /* printf("call\n"); */

    for(i=0; i<len; i++) {
      

        if (strncmp(str1+i, str2, len)==0) {
            /* printf("str1 %s,  str2 %s len%d\n",str1+i, str2, len); */

            isHit = 1;
            break;
        
        }else {
            sameNum=0;
        }
    }

    /* printf("end\n"); */
    /* 文字列str1, str2がlen分同じ文字列を抱えている:0 else 1*/
    if(isHit) {
        
        return i;
    } else {
        return -1;
    }

    
}
/* IPアドレス置換 */
/* X.X.Y.Xという形式のアドレス(X部分は何が入っていてもよい)を置換基アドレスorig_net==Yとし、 */
/* aft_net==ZとしたときX.X.Z.Xに置換する */
static struct in_addr changeIP (struct in_addr ip_addr, int orig_net, int aft_net) {
    char s_orig_ip [20];
    char s_orig_net[5], s_aft_net[5];
    int s_aft_net_size, s_orig_net_size;
    int cnt, end;
    int hit = 0;
    struct in_addr aft_ip;
    int loc = 0;
    
    sprintf(s_orig_ip, "%s",   inet_ntoa(ip_addr));
    sprintf(s_orig_net, ".%d" ,orig_net);
    sprintf(s_aft_net,  ".%d" ,aft_net);

    printf("origip %s, net %s aft %s\n", s_orig_ip, s_orig_net, s_aft_net); /*  */
    s_orig_net_size = strlen(s_orig_net);
    s_aft_net_size  = strlen(s_aft_net);
    end         = strlen(s_orig_ip) - strlen(s_orig_net);

    for(cnt = 0; cnt < end; cnt++) {
        //printf("call %s\n",s_orig_ip+cnt);
        if((loc = isSameStr(s_orig_ip+cnt, s_orig_net, s_orig_net_size)) >= 0) {
            //printf("s_orig_ip+cnt %s,  s_orig_net %s\n",s_orig_ip+cnt, s_orig_net);
            hit = 1;
            break;
        }
        
    }
    /* 置換 */
    if (hit) {
        strncpy(s_orig_ip+cnt+loc, s_aft_net,  s_aft_net_size);
        /* printf("s_aft ip %s\n" */
        /*        ,s_orig_ip); */
    }
    
    inet_aton(s_orig_ip, &aft_ip);

    return aft_ip;              /* パターン一致がない場合、オリジナルのIPアドレスが返る */

}
int main()
{
    char ip1[20] = "192.168.30.254";
    int  orig = 30, aft = 50;
        
    struct  in_addr ipaddr;

    inet_aton(ip1, &ipaddr);
    printf("prig %s\n",inet_ntoa(ipaddr));

    ipaddr = changeIP(ipaddr, orig, aft);
    printf("aft %s\n",inet_ntoa(ipaddr));
    return 0;
}
