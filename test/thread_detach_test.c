#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <signal.h>

size_t const thread_no = 64000000;
char mess[] = "This is a test";

void *message_print(void *ptr){
  int error = 0;
  int signal_exit = (SIGRTMIN+10);
  sigset_t signal_set;
  siginfo_t info;
  struct timespec timer;
  timer.tv_sec = 10;
  timer.tv_nsec = 0;
  int status;
  
   /* get time */
    struct timeval current_time;
    int            day,usec;    
  //  char *msg;
  /*シグナルの登録(queue)*/
    /* sigemptyset     ( &signal_set );  */
    /* sigaddset       ( &signal_set, signal_exit );       // exit sig */
    /* pthread_sigmask ( SIG_BLOCK, &signal_set, 0 ); */
    
  /* スレッドをデタッチ */
  error = pthread_detach(pthread_self());
  /* もしあればエラー処理 */

  //  msg = (char *) ptr;
  gettimeofday(&current_time, NULL);
  day   = (int)current_time.tv_sec;
  usec  = (int)current_time.tv_usec;
  printf("%d.%06d THREAD: [created           ] pid %d tid %d\n",day,usec, getpid(), syscall(SYS_gettid));
  sleep(10);
  /* if ((status = sigtimedwait(&signal_set,&info,&timer)) == signal_exit ){ */
      
  /*     printf("%d.%06d  THREAD: [sigusr1   received] pid %d tid %d\n",day,usec,  getpid(), syscall(SYS_gettid)); */
  /*     pthread_exit(NULL); */
  /* } */
  gettimeofday(&current_time, NULL);
  day   = (int)current_time.tv_sec;
  usec  = (int)current_time.tv_usec;
  printf("%d.%06d THREAD: [sig isn't received] pid %d tid %d\n",day,usec, getpid(), syscall(SYS_gettid));
  pthread_exit(NULL);
}


struct pthread
{
    union
    {
        void *__padding[24];
    };
    
    /* This descriptor's link on the `stack_used' or `__stack_user' list.  */
    struct list_head
    {
        struct list_head *next;
        struct list_head *prev;
    } list;
    
    /* Thread ID - which is also a 'is this thread descriptor (and
       therefore stack) used' flag.  */
    pid_t tid;
    
    /* Process ID - thread group ID in kernel speak.  */
    pid_t pid;
};

int main(void) {
  int error = 0;
  size_t i = 0;
  /* スレッドプールを作成 */
  pthread_t thr, old_thr;
  
  int cancel_status;
  struct pthread *pthr;
  int kill_status;
  sigset_t signal_set;
  pthread_t hoge=0;
  pthread_mutex_t *lock; //なにも示していない場合 どうなるのか？
  pthread_mutex_t mutex;
  int j=0;
  int signal_exit = (SIGRTMIN+10);
  sigemptyset     ( &signal_set ); 
  sigaddset       ( &signal_set, signal_exit );        
  pthread_sigmask ( SIG_BLOCK, &signal_set, 0 ); 
  thr = 0;
  //lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
  mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
  lock = &mutex;
  //  free(lock);
  for(i = 0; i < thread_no; i++) {
  //  for(i = 0; i < 1; i++) {
      //thr = 0;
      pthread_mutex_lock(lock);
      old_thr=thr;
      pthread_mutex_unlock(lock);
      if((error = pthread_create( &thr, NULL, message_print, (void *) mess)) != 0) {
          printf("thread creation error no.%d (EAAIN %d) thread num %d\n",error,EAGAIN,i);
          exit(EXIT_FAILURE);
          
      } else {
          pthr = (struct pthread *)thr;
          printf("thread(%d) creation SUCESS pid %d tid %d\n",i,pthr->pid, pthr->tid);
          
          //printf("sizeof pthread_t %#x\n",sizeof(thr));
      }
      
      printf("sizeof pthread t %d struct pthread %d\n",sizeof(pthread_t), sizeof(struct pthread));
      if(i%3==0) {
          
          if((kill_status = pthread_kill(thr, signal_exit)) != 0) {
              printf("kill failed  status % d;\n",kill_status);
              exit(EXIT_FAILURE);
          }
          //          usleep(1000);
          printf("old_thr = %x\n",old_thr);
          if(old_thr != 0) {
              /* if((kill_status = pthread_kill(old_thr, 0)) != 0) { */
              /*     printf("kill failed  status % d;\n",kill_status); */
              /*     exit(EXIT_FAILURE); */
              /* } */
              kill_status = pthread_kill(old_thr,signal_exit );
              printf(" send signal \n", old_thr);
              
          }
      }
      sleep(1);
            
          //キャンセルしても領域解放されるのかを観察
      //          printf("thread id %#x is canceled\n",thr);
          
       
      
       
    /* エラー処理 */
  }
  //pthread_join(thr, NULL);
  printf("MAIN: Thread Message: %s\n", mess);
  pthread_exit(NULL);
}
