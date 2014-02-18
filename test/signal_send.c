/* gcc signal_send.c -o signal_send -g -W -Wall -lpthread */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>

struct {
    int    sigid;
    char * sigmess;
}   siginfo[] = {
    {   SIGUSR1,   "'SIGUSR1'  Recv.",  },
};

#define array( a )  ( sizeof( a )/sizeof( a[0] ))

void * signal_func( void * arg );

int main( ) {

    pthread_t id1,id2;
    sigset_t  ss;
    size_t    i;
    
    int       rtn;
    int       num1, num2;
    
    //初期化
    sigemptyset( &ss );
    for( i = 0; i < array( siginfo ); i ++ ) {
        //ブロックするシグナル番号の登録
        sigaddset( &ss, siginfo[i].sigid );
    }
    //(SIG_BLOCK) ssに登録したシグナル群を加える(スレッド作成前でないとだめみたい)
    sigprocmask( SIG_BLOCK, &ss, 0 );

    num1=1;
    pthread_create( &id1, 0, &signal_func, &num1 );
    printf("thread %d is created\n",id1);
    num2=2;
    pthread_create( &id2, 0, &signal_func, &num2 );
    printf("thread %d is created\n",id2);

    printf("---\n");
    rtn = pthread_kill( id1, SIGUSR2 );
    for( i = 0; i < array( siginfo ); i ++ ) {
        sleep( 3 );
        pthread_kill( id1, siginfo[i].sigid ); //スレッドにシグナル送信
    }
    rtn = pthread_kill( id2, SIGUSR2 );

    for( i = 0; i < array( siginfo ); i ++ ) {
        sleep( 3 );
        pthread_kill( id2, siginfo[i].sigid ); //スレッドにシグナル送信
    }
    //    rtn = pthread_kill( id1, 0 );
    //    printf( "pthread_kill:[%d][%s]\n", rtn, strerror( rtn ));

    sleep(3);
    return 0;
}

void * signal_func( void * arg ) {
    ( void )arg;
    sigset_t    ss;
    int         sig;
    size_t      i;
    int         *par;
    par = (int)arg;

    //pthread_self 自分のスレッドID取得 detachでスレッド終了後にスレッド用の領域開放
    pthread_detach( pthread_self( ));
    sigemptyset( &ss );
    for( i = 0; i < array( siginfo ); i ++ ) {
        sigaddset( &ss, siginfo[i].sigid );
    }
    //スレッド作成前に登録したシグナルを得ているのか？？？
    pthread_sigmask( SIG_BLOCK, &ss, 0 );
    
    printf("(thread %d [id %d])sigwait (wait for signal %d)\n",pthread_self(),*par, ss);
    while(1) {
        if( sigwait( &ss, &sig )) { //受け取ったシグナルをsigに入れて返すpthread_kill or C-c打つとここに代入されるようす
            printf( "not SIGUSR1 signal!!\n" );
            continue;
        }
        for( i = 0; i < array( siginfo ); i ++ ) {
            printf( "(thread %d [id %d]) %s (%2d) return\n", pthread_self(),*par,siginfo[i].sigmess, sig );
            
        }
        break;
    }
    return;
}
