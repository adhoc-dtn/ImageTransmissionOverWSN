#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/route.h>

#define BUFLEN (sizeof(struct rt_msghdr)+512)
#define SA struct sockaddr

#define SEQ 9999
#define ROUNDUP(a,size)(((a)&((size)-1))?(1+((a)|((size)-1))):(a))
#define NEXT_SA(ap)ap=(SA *)\
  ((caddr_t)ap+(ap->sa_len?ROUNDUP(ap->sa_len,sizeof(u_long)):\
		sizeof(u_long)))

void get_rtaddrs(int addrs,SA *sa,SA **rti_info){
  int i;
  for(i=0;i<RTAX_MAX;i++){
    if(addrs & (1<<i)){
      rti_info[i]=sa;
      NEXT_SA(sa);
    }else rti_info[i]=NULL;
  }
}

char *sock_ntop_host(const struct sockaddr *sa,socklen_t salen){
  char portstr[7];
  static char str[128];
  
  switch(sa->sa_family){
  case AF_INET:{
    struct sockaddr_in *sin=(struct sockaddr_in *)sa;
    if(inet_ntop(AF_INET,&sin->sin_addr,str,sizeof(str))==NULL)
      return(NULL);
    if(ntohs(sin->sin_port)!=0){
      snprintf(portstr,sizeof(portstr),".%d",ntohs(sin->sin_port));
      strcat(str,portstr);
    }
    return(str);
  }
  }
}
char *sock_masktop(SA *sa,socklen_t salen){
  static char str[INET6_ADDRSTRLEN];
  unsigned char *ptr=&sa->sa_data[2];
  int sula,box1,box2;
  sula=(((sa->sa_len)-5)*8);
  box1=256-*(ptr+(sula/8));
  sula=sula+8;
  while(box1!=1){
    sula=sula-1;
    box1=box1/2;
  }
  snprintf(str,sizeof(str),"%d",sula); 
  return(str);
}

int main(int argc,char **argv){
  int sockfd;
  int check=0;
  char *buf;
  pid_t pid;
  ssize_t n;
  struct rt_msghdr *rtm; 
  struct sockaddr *sa,*rti_info[RTAX_MAX];
  struct sockaddr_in *sin;
  if(argc!=2){
    printf("Usage : ./a.out <IP address>\n");
    exit(0);
  }
  
  sockfd=socket(AF_ROUTE,SOCK_RAW,0);
  buf=(char *)malloc(BUFLEN);
  rtm=(struct rt_msghdr *)buf;
  rtm->rtm_msglen=sizeof(struct rt_msghdr)+sizeof(struct sockaddr_in);
  rtm->rtm_version=RTM_VERSION;
  rtm->rtm_type=RTM_GET;
  rtm->rtm_addrs=RTA_DST;
  rtm->rtm_pid=pid=getpid();
  rtm->rtm_seq=SEQ;
  sin=(struct sockaddr_in *)(rtm+1);
  sin->sin_len=sizeof(struct sockaddr_in);
  sin->sin_family=AF_INET;
  inet_pton(AF_INET,argv[1],&sin->sin_addr);
  write(sockfd,rtm,rtm->rtm_msglen);
  do{
    n=read(sockfd,rtm,BUFLEN);
  }while(rtm->rtm_type!=RTM_GET||rtm->rtm_seq!=SEQ||rtm->rtm_pid!=pid);
  rtm=(struct rt_msghdr *)buf;
  sa=(struct sockaddr *)(rtm+1);
  get_rtaddrs(rtm->rtm_addrs,sa,rti_info);
  if((sa=rti_info[RTAX_DST])!=NULL)
    printf(" Dist    < %s",sock_ntop_host(sa,sa->sa_len));
  if((sa=rti_info[RTAX_NETMASK])!=NULL){
    printf("/%s >\n",sock_masktop(sa,sa->sa_len));
    check=1;
  }
  if((sa=rti_info[RTAX_GATEWAY])!=NULL){
    printf(" Gateway < %s >\n",sock_ntop_host(sa,sa->sa_len));
    check=1;
  }
  if(check==0)
    printf(" >\n No Routing Table\n");
  exit(0);
}
