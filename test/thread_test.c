#include        <stdio.h>
#include        <sys/types.h>
#include        <pthread.h>

void *counter(void *arg)
{
  int     i;
  pid_t   pid;
  pthread_t	thread_id;

  pid=getpid();
  thread_id=pthread_self();

  for(i=0;i<10;i++){
    sleep(1);
    printf("[%d][%d]%d\n",pid,thread_id,i);
  }

  return(arg);
}

void thread_create()
{
  pid_t   p_pid;
  pthread_t	thread_id1,thread_id2;
  int	status;
  void 	*result;
  
  int  return_val;
  int  status_kill;
  
  p_pid=getpid();

  printf("[%d]start\n",p_pid);

  status=pthread_create(&thread_id1,NULL,counter,(void *)NULL);
  if(status!=0){
    fprintf(stderr,"pthread_create : %s",strerror(status));
  }
  else{
      printf("[%d]thread_id1=%d\n",p_pid,thread_id1);
  }

  status=pthread_create(&thread_id2,NULL,counter,(void *)NULL);
  if(status!=0){
    fprintf(stderr,"pthread_create : %s",strerror(status));
  }
  else{
    printf("[%d]thread_id2=%d\n",p_pid,thread_id2);
  }

  
  sleep(5);
  if((  status_kill = pthread_kill(thread_id1,SIGUSR1) != 0)){
      err(EXIT_FAILURE,"send signal fail\n");
  }
  /*スレッドのキャンセル*/
  //  return_val = pthread_cancel(thread_id1);
  //  printf("thread1 cancel val = %d\n",return_val);
  //return_val  = pthread_cancel(thread_id1);
  //printf("thread1 cancel val = %d\n",return_val);


  /* pthread_join(thread_id1,&result);
     printf("[%d]thread_id1 = %d end\n",p_pid,thread_id1);*/
  //pthread_join(thread_id2,&result);
  //  printf("[%d]thread_id2 = %d end\n",p_pid,thread_id2);
  
  //printf("[%d]end\n",p_pid);
}

int main()
{
    thread_create();
    printf("[main] thread created\n");
    sleep(5);
    
    sleep(10);
    printf("[main] exti\n");
    return 0;
}
