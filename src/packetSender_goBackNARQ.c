/*Time-stamp: <Tue Jan 07 11:17:53 JST 2014>*/

/*[TODO]
 * X1 RTOを適応的に変化させる（RFC793）
 * X2 Window Size は現在固定値だが、画像収集ノードの制御メッセージにより、輻輳時には減少
 *    回復時には増加させる
 * X3 Selective and repeat ARQ (TCP SACKのような)の実装(Go back N ARQより効率的)
 *
 */
#include "common.h"

/* 次ホップノード取得用スクリプト
 * (オプションなしですべての次ホップリスト、
 * ノードの親子関係、宛先IPアドレス指定でその宛先への次ホップが得られる)*/
#define GET_NEXT_HOP_SCRIPT          "./get_next_node.pl"
/*1ホップ隣接子ノードリストを記録するファイル*/
#define ONEHOP_CHILDREN              "onehop_children.dat"
/*1ホップ隣接親ノードを記録sするファィル*/
#define ONEHOP_PARENT                "onehop_parent.dat"

/* 再送時のパラメータ */
/* RFC 793、TCP(linux)の実装を参考に、以下のパラメータを設定する */
/* #define ARQ_ALPHA     () */
/* #define ARQ_BETA      () */
/* /\* SRTT初期値 *\/ */
/* #define ARQ_SRTT_INIT () */
/* /\* Upper bound *\/ */
/* #define ARQ_UBOUND    () */
/* /\* Lower bound *\/ */
/* #define ARQ_LBOUND    () */
/* RTOの最大値 */
#define ARQ_UBOUND    (60.0)

/*上記コマンド実行によるファイル書き込みおよび読み出しの排他制御用*/
static pthread_mutex_t route_file_read_write_mutex = PTHREAD_MUTEX_INITIALIZER;


/* 以下のリスト、すべてスレッド内にて、ヒープ領域にメモリを確保する */
/* 項目削除時は、mallocで確保した分すべてfreeすること             */
/*送信パケットキュー*/
struct tx_packet {
    char                     *packet;           /* [要malloc & free](シーケンス番号)+(パケット本体)                        */
    u_short                   packetSize;       /* シーケンス番号も含めたパケットサイズ                                     */
    u_short                   sequenceNumber;   /*シーケンス番号本体                                                       */
    double                    timerResend;      /* 再送タイマ                                                             */
    u_short                   maxResend;        /* 再送上限                                                               */
    pthread_mutex_t          *queueMutex;       /* 排他制御のmutex(実体は対応する次ホップアドレスリストのmutexにする)        */
    struct sockaddr_in        destAddr;         /* 宛先端末アドレス構造体                                                  */
    int                      *destSock;         /* 送信用ソケット（次ホップアドレスリスト中に実体にもつ                       */
    pthread_t                 threadID_sender;   /* sendWaitAndResendスレッドのID                                          */
    u_short                   isThreadExists_sender;/* 上記のスレッドが現在起動中であるか                                   */
    pthread_t                *threadID_delQueue;       /* キュー要素削除スレッドID(次ホップ情報リストを参照する)             */
    pthread_t                *threadID_resender;       /* 再送要求スレッド */
    u_short                   isThreadExists_delQueue; /* 上記のスレッドが現在起動中であるか                                */
    pthread_t                 threadID_createSender;   /*パケット送信用スレッドを作る、作り元のスレッドID(パケット送信順序制御用)*/
    u_int                     numMiddleSend;             /* 次ホップ情報リスト中の、送信中スレッド数を参照 */
    struct tx_packet         *prev;              /* キュー要素、先頭(先に送信されるもの)側から末尾方向へのポインタ             */
    struct tx_packet         *next;              /* キュー要素、末尾側から先頭方向へのポインタ                                */
};

/* 次ホップ情報リスト*/
struct next_hop_addr_info {
    u_short                   protocolType; /* 送信に用いるプロトコル種別 */
    u_short                   packetType;   /* パケットタイプ */
    u_short                   listNumber;                 /* 項目No                                                  */
    u_short                   typeARQ;                    /*再送ポリシ(ACK or NACK)                                   */
    
    pthread_t                 threadID_unicastWithARQ;    /* キュー要素の送信スレッド（上記スレッド作成用スレッド）のID */
    u_short                   isThreadUWAExists;          
    pthread_t                 threadID_recvACKorNACK;     /* ACK受信用スレッド */
    u_short                   isThreadRACKExists;         
    pthread_t                 threadID_resender;          /* 再送用スレッド */
    u_short                   isThreadRSExists;           
    pthread_t                 threadID_delQueue;          /* キュー要素削除スレッドID (実体)            */
    u_short                   isThreadDQExists;           

    u_short                   waitingEnqueueSig;          /* エンキューシグナル待ち状態であることを表す    */
    u_short                   waitingDequeueSig;          /*デキューシグナル待ち状態であることを表す       */

    struct in_addr            ipDest;       /* パケット宛先IPアドレス                                            */
    struct in_addr            ipMy;         /* 自ノードIPアドレス(ACK受信時に使う)                                */
    int                       destSock;      /* データ送信用ソケット(実体)                                        */
    struct tx_packet          queueHead;    /* 送信キューヘッド(next:末尾 prev:先頭を表す(この実体には要素を入れない) */
    pthread_mutex_t           queueMutex;   /* キューの排他制御のmutex(実体) 項目作成時にPTHREAD_MUTEX_INITIALIZER */
    u_short                   destPort;     /* 宛先ポート                                                      */
    u_short                   recvPort;     /* ACK受信用ポート                                                 */
    u_int                     queueSize;    /* キューサイズ                                                    */
    u_short                   nonBlocking;  /* ノンブロッキング送信時（画像データ送信時）、1 ブロッキング、0      */
    u_int                     windowSize;   /* ACKを待たずに一度に送信処理できるパケット数                      */
    u_int                     numMiddleSend;/* 現在同時送信処理しているパケット数 windoSize未満となる             */
    u_int                     numPendinginCache; /*(NACK利用時使用のパラメータ)送信終了後にキューに残存するパケット数*/
    struct next_hop_addr_info *next;
};

/*各メッセージ(common.h 内参照)毎に隣接ノードリストのヘッドを設ける */
/* 初期化しなくてよかった？スレッドで初期化しておくとよい*/
static struct next_hop_addr_info  Next_Hop_Addr_Info_Head [PACKET_TYPE_NUM];
static u_short                    SequenceNumber          [PACKET_TYPE_NUM];
pthread_mutex_t                   SequenceNumberMutex     [PACKET_TYPE_NUM];

//ACK用ソケットリスト(ACK送信経験のある宛先IP、ポート宛の送信用ソケットを使いまわす)
struct ack_sock_item {
    struct sockaddr_in    destAddr;   //宛先アドレス情報（IPアドレス、宛先ポート番号
    int                   destSock;   //ソケット

    /*以下、typeACK==NACKの場合に使用するパラメータ*/
    u_short               lastSeq;     //前回受信したパケットのシーケンス番号
    u_short               expectedSeq; //今回受信を期待するシーケンス番号(上記+1)
    struct tx_packet      queueHead;  //送信中NACKパケットキュ(next:末尾(新しい)、prev 先頭を示す これ以外に要素を入れない)
    pthread_mutex_t       queueMutex;        //上記キュー要素操作時のmutex（実体）
    pthread_t             threadID_delQueue; //NACKパケットキューの削除
    u_int                 queueSize;         //キューサイズ
    u_int                 windowSize;  //同時送信処理可能のパケット数(NACKの場合は必要以上に大きくする必要はない。はず)
    u_int                 numMiddleSend;     //現在送信処理しているパケット数(=アクティブな送信スレッド数)
    pthread_t             threadID_sendNACK;    /* NACK送信スレッド（上記スレッド作成用スレッド）のID */
    u_short               isThreadsNACKExists; /* 上記スレッド動作状況 THREAD_READY or  THREAD_START     */
    u_short               waitingEnqueueSig; /* エンキューシグナル待ち状態であることを表す             */
    u_short               waitingDequeueSig; /*デキューシグナル待ち状態であることを表す                */
    struct ack_sock_item *next;
};
//上記リストヘッド（実体）
static struct ack_sock_item Ack_Sock_Item_Head;



/*次ホップにパケットを送信する*/
/*  子ノード ot 親ノードにパケットを送信する
 *  @param struct sendingPacketInfo
 *  u_short        nextNodeType;     次ホップが子ノード:CHILDLEN, 親ノード:PARENT   
 *  u_short        packetType;       送信パケットタイプ(下部enum内の中から指定) 
 *  struct in_addr ipAddr;           CHILEDLEN(親ノードのIPv4アドレスを指定 親がいなければ割り振られていないIPアドレス入れる) PARENT(画像収集ノードのIP) 
 *  u_short        destPort;         パケット送信の宛先ポート                
 *  u_short        dataReceivePort; （中継時に指定）パケット受信用ポート     
 *  u_short        ackPortMin;       ACK受信用ポート最小値                  
 *  u_short        ackPortMax;       ACK受信用ポート最大値                  
 *  char          *packet;           送信するパケット                       
 *  u_short        packetSize;       送信パケットサイズ
 */
void sendPacketToNext (struct sendingPacketInfo sPInfo);

/*パケットを次ホップに転送する*/
/*  子ノード ot 親ノードにパケットを送信する
 *  @param struct *sendingPacketInfo
 *  u_short        nextNodeType;     次ホップが子ノード:CHILDLEN, 親ノード:PARENT   
 *  u_short        packetType;       送信パケットタイプ(下部enum内の中から指定) 
 *  struct in_addr ipAddr;           CHILEDLEN(親ノードのIPv4アドレスを指定 親がいなければ割り振られていないIPアドレス入れる) PARENT(画像収集ノードのIP) 
 *  u_short        destPort;         パケット送信の宛先ポート                
 *  u_short        dataReceivePort; （中継時に指定）パケット受信用ポート     
 *  u_short        ackPortMin;       ACK受信用ポート最小値                  
 *  u_short        ackPortMax;       ACK受信用ポート最大値                  
 *  char          *packet;           送信するパケット(いらない)
 *  u_short        packetSize;       送信パケットサイズ（いらない）
 */
void *relayPacket (void *sPInfo);

/*指定インタフェースに割り当てられているIPアドレスを取得する*/
struct in_addr GetMyIpAddr(char* device_name);

/*ACK*/
int  SendingACKorNACK (u_short typeACK, struct in_addr destIP,
                       u_short destPort, char message[],
                       u_short messSize, u_short isResend);


/*以下、このファイル内で使用している関数であり、外部ファイルから使用できない(する必要もない)*/
/*APPレイヤでのARQ込のユニキャスト*/
static void *UnicastWithARQ    (void *param);
/* パケット送信後、タイマ時間待ったあとで再送信 再送上限まで再送し、終了*/
static void *sendWaitAndResend (void *param);
/* go back n arq適用時、タイムアウトを起こしたパケットから再送を開始する */
static void *packetResender    (void *param);
/*ACKを受信する*/
static void *recvACKorNACK     (void *param);
/* パケットをキューに挿入*/
static void  UnicastPacket    (struct sendingPacketInfo sPInfo,
                               struct next_hop_addr_info *nextHopAddrInfo);

/* 指定シーケンス番号のパケットをキューから削除*/
static void *delPacketFromQueue  (void *param);
static void *sendingNACK         (void *param);
static void *delNACKfromQueue    (void *param);
static void *recvTCPPacket       (void *param);
/* 文字列パターン一致検索 */
static int isSameStr(char *str1, char *str2, int len);
/* IPアドレス変換 */
static struct in_addr changeIP (struct in_addr ip_addr, int orig_net, int aft_net);


//この関数、mainなど、１回しか実行されない場所に置く
void initializePacketSender() {
    int     mutex_max = PACKET_TYPE_NUM;
    int     mutex_num;

    /*プロセスでデフォルト動作をブロックするシグナルの登録*/
    /* common.hでシグナルを定義したら更新する */
    /* ただし、SIGINTやSIGSEGV等、プロセスの正常動作に必要なシグナルをブロックしてはならない */
    /* シグナルは kill -l で確認 */
    int                   realSigAll = 9;
    int                   realOffSet = 7;
    sigset_t              signal_set;
    int                   cnt;
    
    sigemptyset     ( &signal_set );
    sigaddset       ( &signal_set, SIGUSR1 );       
    sigaddset       ( &signal_set, SIGUSR2 );
    for(cnt = 0; cnt < realSigAll; cnt++) {
        sigaddset       ( &signal_set, SIGRTMIN+realOffSet+cnt);
    }
    
    pthread_sigmask ( SIG_BLOCK, &signal_set, 0 );


    //mutexの初期化
    for(mutex_num = 0; mutex_num<mutex_max; mutex_num++) {
        SequenceNumberMutex[mutex_num] = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
    }
}


/*
 * パケット送信後、タイマ時間スリープして、再送
 * 再送上限に達すると、スレッド終了
 * また、create元が対応するACKを受信するとキャンセルされる
 * @param struct tx_packet
 *  char                *packet;          [要malloc & free](シーケンス番号)+(パケット本体)                        
 *  u_short              packetSize;     シーケンス番号も含めたパケットサイズ                                     
 *  u_short              sequenceNumber; シーケンス番号本体                                                       
 *  pthread_mutex_t     *queueMutex;     排他制御のmutex(実体は対応する次ホップアドレスリストのmutexにする)        
 *  struct sockaddr_in   servAddr;       宛先端末アドレス構造体                                                  
 *  pthread_t            threadId;       sendWaitAndResendスレッドのID                                          
 *  u_short              isThreadExits;   上記のスレッドが現在起動中であるか否かを示す(THREAD_READY ,THREAD_EXIT, THREAD_START)
 *  double               timerResend;     再送タイマ                                                             
 *  u_short              maxResend;       再送上限
 *  u_short              nonBlocking;  /* ノンブロッキング送信時（画像データ送信時）、1 ブロッキング、0 
 *  pthread_t            threadID_createSender; /*パケット送信用スレッドを作る、作り元のスレッドID(パケット送信順序制御用)
 * (以下はこのスレッドでは未使用)
 *  struct tx_packet    *prev;            キュー要素、先頭(先に送信されるもの)側から末尾方向へのポインタ             
 *  struct tx_packet    *next;            キュー要素、末尾側から先頭方向へのポインタ                                
 */
static void *sendWaitAndResend (void *param) {
    int                   destSock;
    int                   destSockLen;
    int                   bindStatus;
    struct tx_packet     *tx_packet;
    u_short               destPort;
    char                 *packet;
    
    //スレッドのデタッチステータス
    int                   detach_status;
    //パケット再送
    int                   numSendBytes; 
   
   
    /* 送信シグナル */
    int                   signal_end_of_send     = SIGNAL_SENDEND; /* ACK受信（送信成功）シグナル */
    sigset_t              signal_set;
    int                   signal_del_queue       = SIGNAL_DEL_QUEUE; /* キュー要素削除シグナル */
    sigset_t              signal_set_delQ;
    int                   signal_send_one_packet = SIGNAL_SEND_ONE_PACKET; /* 次のパケットの送信スタンバイ */
    sigset_t              signal_set_sender;

    /* 再送時、送信スレッド間で同期を取るためのシグナル */
    int                   signal_send_suc        = SIGNAL_SUCCESS;
    int                   signal_send_fail       = SIGNAL_FAIL;
    sigset_t              signal_set_send_status;
    int                   signal_resend        = SIGNAL_RESEND; /* タイムアウトのシグナル */
    sigset_t              signal_set_resend;
    /* sigwaitの受信シグナル */
    int                   recvSig;
    /* 受信シグナル情報を格納 */
    int                   sig_status;
    siginfo_t             info;
    //再送タイマ
    double                timer_raw;
    double                timer_int, timer_frac;
    struct timespec       time_out;
    int                   numRetry  = 0;
    //時刻記録用
    struct timeval        current_time;
    int                   day,usec;
    double                time_before_wait, time_after_wait;
    int                   recvStatus_prevPac = 0;
    int                   isResend = 0; /* パケットが最初の送信:0 再送中:1 */
    int                   signalResend_alreadyRecvd = 0;
    
    /* 再送時のスレッド間同期確保用シグナル*/
    sigemptyset     ( &signal_set_send_status );
    sigaddset       ( &signal_set_send_status, signal_send_suc);
    sigaddset       ( &signal_set_send_status, signal_resend);
    pthread_sigmask ( SIG_BLOCK, &signal_set_send_status, 0 );

    sigemptyset     ( &signal_set_sender );
    sigaddset       ( &signal_set_sender, signal_send_one_packet ); /* キュー内の次要素の送信許可シグナル */
    pthread_sigmask ( SIG_BLOCK, &signal_set_sender, 0 );
    
    sigemptyset     ( &signal_set );
    sigaddset       ( &signal_set, signal_end_of_send );          /* ACK受信シグナルのブロック */
    sigaddset       ( &signal_set, signal_resend);                 /* 再送開始シグナル */
    pthread_sigmask ( SIG_BLOCK, &signal_set, 0 );

    sigemptyset     ( &signal_set_delQ );
    sigaddset       ( &signal_set_delQ, signal_del_queue );         /* キュー要素削除シグナルのブロック */
    pthread_sigmask ( SIG_BLOCK, &signal_set_delQ, 0 );

   
    
    //デタッチ状態にし、スレッド終了後に本スレッドのスタック領域が解放されるようにする
    //(本スレッドは本プログラムで何度もcreate&終了を繰り返すので、これがないとthreadの作成上限に達する)
    if((detach_status = pthread_detach(pthread_self())) != 0) {
        err(EXIT_FAILURE, "thread detach is faild (sendWaitAndResend)\n");
    }
    /* RTOの設定 */
    tx_packet                        =  (struct tx_packet *)param;
    timer_raw                        = tx_packet->timerResend;
    timer_frac                       = modf(timer_raw,  &timer_int);
    time_out.tv_sec                  = (int)timer_int;
    timer_frac                       *= 1000000000; //単位をnsecに
    time_out.tv_nsec                 = (int)timer_frac;


  
    tx_packet->isThreadExists_sender = THREAD_START;
        
    do {
        /*ACK受信までパケットを再送*/
        sig_status = -10;     //デフォルト(1 - 64（シグナルとして定義されている正数）以外の値を入れておく)
        

        /* 再送時の処理 */
        if (isResend > 0) {

            /* 再送開始シグナルを未受信 */
            if (signalResend_alreadyRecvd == 0) {
                /* !-lock  */
                /* キュー操作中のキューの前後参照防止。およびその逆も防止のためロック */
                pthread_mutex_lock(tx_packet->queueMutex);
                /*
                  前のパケットがあれば、再送タイミングの同期を取る  (成功or再送処理おわり（再送の成功判断はできない)   
                  (前がヘッドの場合は[THREAD_NON])
                */
            
                if(tx_packet->next->isThreadExists_sender == THREAD_START) {
                    pthread_mutex_unlock(tx_packet->queueMutex);
                    /* sigwaitの前にロックを外さなければならない */
                    sigwait(&signal_set_send_status, &recvSig);
                } else {
                    pthread_mutex_unlock(tx_packet->queueMutex);
                }
                /* !-unlock  */

            }
            //送信ログ
            gettimeofday(&current_time, NULL);
            day   = (int)current_time.tv_sec;
            usec  = (int)current_time.tv_usec;
            
            printf("[%d.%06d sendWaitAndResend %#x] (timeout) resend sequence %d (try %d timer %d.%06d s MiddleSend %d)\n"
                   ,day
                   ,usec
                   ,pthread_self()
                   ,tx_packet->sequenceNumber
                   ,numRetry
                   ,time_out.tv_sec
                   ,time_out.tv_nsec
                   ,tx_packet->numMiddleSend);
            

        }
     
        
        numSendBytes = sendto(*(tx_packet->destSock),
                              tx_packet->packet,
                              tx_packet->packetSize,
                              0,
                              (struct sockaddr *)&tx_packet->destAddr,
                              sizeof(tx_packet->destAddr));
            
        
   
        //次のパケット送信(スレッド作成)を許可する(最初の送信に限る)
        if(isResend == 0 ) {
            pthread_kill(tx_packet->threadID_createSender, signal_send_one_packet);
        } else if (isResend > 0 ) { /* 次のパケットの再送許可を出す */

            /* 次のパケットに再送開始シグナルを送る*/
            pthread_mutex_lock(tx_packet->queueMutex);
            if(tx_packet->prev->isThreadExists_sender == THREAD_START) {
                pthread_kill(tx_packet->prev->threadID_sender, signal_resend);
            }
            pthread_mutex_unlock(tx_packet->queueMutex);
        }
    
        //送信ログ
        gettimeofday(&current_time, NULL);
        day   = (int)current_time.tv_sec;
        usec  = (int)current_time.tv_usec;
        printf("[%d.%06d, sendWaitAndResend %#x] sending sequence %d stat %d (try %d timer %d.%06d MiddleSend %d)\n"
               ,day
               ,usec
               ,pthread_self()
               ,tx_packet->sequenceNumber
               ,numSendBytes
               ,numRetry
               ,time_out.tv_sec
               ,time_out.tv_nsec
               ,tx_packet->numMiddleSend);
        time_before_wait = day + (double)usec/1000000;

        
        //RTOまでwait or 前のパケットが再送のシグナルを送る
        sig_status = sigtimedwait(&signal_set,&info,&time_out);
   
        /* pthread_mutex_lock(tx_packet->queueMutex);//!-lock  */
        if( sig_status < 0 || sig_status == signal_resend) {
            /* タイムアウト:-1, 前のパケットが送信失敗: sig_send_fail */
            /* printf("[sendWaitAndResend %#x] (timeout) fail to send sequence %d (try %d timer %d.%06d s)\n" */
            /*        ,pthread_self() */
            /*        ,tx_packet->sequenceNumber */
            /*        ,numRetry */
            /*        ,time_out.tv_sec */
            /*        ,time_out.tv_nsec); */
            
            isResend = 1;       /* 再送状態 */
            numRetry++;

            /* 再送RTOは上限1minutesとする(RFC793には明確な数値は指定されていない e.g. 1 minutesとするというふうに) */
            timer_raw                        = 2*timer_raw;
            if(timer_raw > ARQ_UBOUND) {
                timer_raw = ARQ_UBOUND;
            }
            timer_frac                       = modf(timer_raw,  &timer_int);
            time_out.tv_sec                  = (int)timer_int;
            timer_frac                       *= 1000000000; //単位をnsecに
            time_out.tv_nsec                 = (int)timer_frac;

          
           
            if(sig_status == signal_resend) {
                /* この場合は、再送時に前のパケットからの再送開始シグナルを待たなくてもよい */
                signalResend_alreadyRecvd = 1;
            } else {
                /* 再送開始シグナルは受信しない（本スレッドがタイムアウトしただけ） */
                signalResend_alreadyRecvd = 0;
            }
           
        } 
    } while (sig_status != signal_end_of_send);

    
    /* もしも条件に[再送回数 >= 再送上限]を付加する場合、ACKを返す条件を[無条件]にする修正が必要 */
    /* 送信成功シグナルを後続のスレッドに送信する */
    
    pthread_mutex_lock(tx_packet->queueMutex);

    if(tx_packet->prev->isThreadExists_sender == THREAD_START) {
        pthread_kill(tx_packet->prev->threadID_sender, signal_send_suc);
    }
    pthread_mutex_unlock(tx_packet->queueMutex);


    gettimeofday(&current_time, NULL);
    day   = (int)current_time.tv_sec;
    usec  = (int)current_time.tv_usec;
    time_after_wait = day + (double)usec/1000000; /* この値の統計を取ると、RTOの動的変更が可能*/

    printf("[sendWaitAndResend %#x] %d.%06d (takes %.10lf sec) successfully sent sequence %d\n"
           ,pthread_self()
           ,day
           ,usec
           ,time_after_wait-time_before_wait
           ,tx_packet->sequenceNumber);
        
    /*キュー要素削除シグナル送信*/
    
        
    if(pthread_kill(*(tx_packet->threadID_delQueue), signal_del_queue) != 0){
        err(EXIT_FAILURE, "fail to send signal to delQueue (sendWaitAndResend)\n");
    }
    tx_packet->isThreadExists_sender = THREAD_EXIT; /* キュー要素削除可能状態 */    
           
    /* pthread_mutex_unlock(tx_packet->queueMutex); //!-unlock */
    //pthread_exit(NULL); //明示的リソースリサイクル(しなくても行われる)
}


/*
 * ACKの受信を待つ ACKを受信すると再送制御スレッドをキャンセルし、対応するシーケンスナンバの
 * パケットをキューから削除する
 * @param param      ある1つの送信先に対応する(struct next_hop_addr_info型)ポインタ
 * u_short                   listNumber;    項目No
 * struct in_addr            ipDest;       パケット宛先IPアドレス                           
 * struct in_addr            ipMy;          自ノードIPアドレス                                
 * struct tx_packet          queueHead;     送信キューヘッド(next:末尾 prev:先頭以外に要素を入れない   
 * pthread_mutex_t           queueMutex;    キューの排他制御のmutex(実体)
 * u_short                   destPort;      宛先ポート                                     
 * u_short                   recvPort;     送信元ポート&ACK受信用ポート                     
 * u_int                     queueSize;     キューサイズ                                    
 * struct next_hop_addr_info *next;
 */
static void *recvACKorNACK (void *param) {

    int                       numrcv;
    struct sockaddr_in        myAddr;             /* ACK受信用(bind)                             */
    int                       myAddrLen;
    u_short                   myPort;             /* 受信用ポート                                 */
    int                       mySock;             /* 受信用ソケットディスクリプタ                  */
    char                      ackMess[ACK_SIZE];  /* 受信パケット(u_short型2bytesシーケンス番号)   */
    u_short                   sequenceNumber;     /* ACKをu_short型へ                             */
    //ソケット再利用
    int                       True = 1;
    int                       bindStatus;
    /*型変換用*/
    struct next_hop_addr_info *nextHopInfo;
    /*送信キューnext(curの一つ後方のポインタ),cur(操作対象のポインタ)とする*/
    struct tx_packet          *q_next, *q_cur;
    /*port open signal */
    sigset_t                  signal_set;
    int                       signal_port_open       = SIGNAL_PORT_OPEN; //ポートオープン通知
    int                       signal_end_of_send     = SIGNAL_SENDEND;   //送信中断通知
    int                       signal_send_one_packet = SIGNAL_SEND_ONE_PACKET; //次のパケットの送信スタンバイ
    sigset_t                  signal_set_sender;

    struct pthread           *thread_ptid;
    //debuf param
    int      numSearch;
    u_short  resend_pac  = RESEND_PAC;

    //時刻記録用
    struct timeval current_time;
    int            day,usec;
    double time_before_lock, time_after_lock;
    int    time_send, time_received;
    
    //ブロック対象シグナル(シグナルの送信漏れ?がある場合にデフォルト動作するのを防ぐ)
    sigemptyset     ( &signal_set );
    sigaddset       ( &signal_set, signal_port_open );       
    sigaddset       ( &signal_set, signal_end_of_send );       
    pthread_sigmask ( SIG_BLOCK, &signal_set, 0 );
    
    //引数型変換
    nextHopInfo  = (struct next_hop_addr_info *)param;
    myPort       = nextHopInfo->recvPort;
   
    //NACK用
    if(nextHopInfo->typeARQ == NACK) {
        sigemptyset     ( &signal_set_sender );
        sigaddset       ( &signal_set_sender, signal_send_one_packet );       // (自身が送るためにブロック)
        pthread_sigmask ( SIG_BLOCK, &signal_set_sender, 0 );
        pthread_kill(pthread_self(), signal_send_one_packet);
    }
    memset(&myAddr, 0, sizeof(myAddr));
    myAddr.sin_port        = htons(myPort); 
    myAddr.sin_family      = AF_INET;
    myAddr.sin_addr.s_addr = htonl(INADDR_ANY); //ここ次ホップノードからというふうにしておくとよいのかも
    myAddrLen              = sizeof(myAddr);
    if ((mySock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) 
        err(EXIT_FAILURE,"fail to create socket for ACK\n");     /*UDP*/

    setsockopt(mySock, SOL_SOCKET, SO_REUSEADDR, &True, sizeof(int));
    if ((bindStatus   = bind(mySock, (struct sockaddr *) &myAddr, sizeof(myAddr))) < 0 ){
        err(EXIT_FAILURE,"error while bind socket for receiving ack port %d\n",myPort);
    }

    /*bindしてから、*/
    if (pthread_kill(nextHopInfo->threadID_unicastWithARQ, signal_port_open) != 0) {
        err(EXIT_FAILURE, "fail to send signal to the unicastWithARQ thread\n");
    }
    //printf("[recvACKorNACK thread %#x]port %d open signal send.\n",pthread_self(), myPort);
    
    /*  受信したACK中のシーケンス番号と対応する番号のパケットを送信&再送制御しているスレッドをキャンセル*/
    /*  もしも終了(EXIT)であれば、何もしない 1へ*/
    /*(ACK受信用)アドレス構造体の作成*/
    
    while(1) {
        /*ACKシーケンス番号取得 対応するパケットをキューから削除*/
        numrcv = recvfrom(mySock, ackMess, sizeof(ackMess), 0, (struct sockaddr *) &myAddr, &myAddrLen);
       
        memcpy (&sequenceNumber, ackMess, sizeof(ackMess));
        gettimeofday(&current_time, NULL);
        day   = (int)current_time.tv_sec;
        usec  = (int)current_time.tv_usec;
        /* time_before_lock = day + (double)usec/1000000; */
        
        printf("(recvACKorNACK %#x) %d.%06d ACK received seq %d\n"
               ,pthread_self()
               ,day
               ,usec
               ,sequenceNumber
        );
        //シーケンス番号に対応するキュー要素を探索
        /*!-lock-!*/
        pthread_mutex_lock(&nextHopInfo->queueMutex);
        q_next      = &nextHopInfo->queueHead;
        q_cur = nextHopInfo->queueHead.prev;

        //探索中に、(先頭部分から)削除される恐れがあるためロック
        numSearch = 0;
        while( q_cur != &nextHopInfo->queueHead ) { 
            /* printf("(recvACKorNACK %#x) now seq %d searching seq %d   thread's state is  %d (start %d) \n" */
            /*        ,pthread_self() */
            /*        ,q_cur->sequenceNumber */
            /*        ,sequenceNumber */
            /*        ,q_cur->isThreadExists_sender */
            /*        ,THREAD_START); */
            //該当シーケンスナンバがキュー内にある
            if(q_cur->sequenceNumber == sequenceNumber) {
               
                //受信したパケット＝ACK
                if(nextHopInfo->typeARQ == ACK
                   && q_cur->isThreadExists_sender == THREAD_START) { //スレッドが起動中であれば中断シグナルを送信
                    /* gettimeofday(&current_time, NULL); */
                    /* day   = (int)current_time.tv_sec; */
                    /* usec  = (int)current_time.tv_usec; */
                    /* printf("(recvACKorNACK %#x) %d.%06d send signal to seq %d  (numSearch %d) \n" */
                    /*        ,pthread_self() */
                    /*        ,day */
                    /*        ,usec */
                    /*        , q_cur->sequenceNumber */
                    /*        , numSearch */
                    /* ); */
                    //pthread_killのステータスは無視のためエラー処理しない
                    pthread_kill(q_cur->threadID_sender, signal_end_of_send);
                    break; //対応するパケット発見なのでrecvfromへ
                    
                } else if (nextHopInfo->typeARQ == NACK) { //NACKの受信
                    //ここ、前の送信スレッドがまだ生きていると、ここ経由してもexitのマークに代わる恐れあり
                    q_cur->isThreadExists_sender = THREAD_START;
                    memcpy(q_cur->packet+2*sizeof(u_short), &resend_pac ,  sizeof(u_short)); //4~5bytes目をresendマークにする
                    printf("[recvACKorNACK %#x] NACK received. resending seq %d %#x\n"
                           ,pthread_self()
                           ,q_cur->sequenceNumber
                           ,q_cur->sequenceNumber);
                    //送信スレッドからシグナルが送信される(1回目は受信済み)
                    sigwait(&signal_set_sender, &signal_send_one_packet );
                    printf("[recvACKorNACK] received enable create thread signal\n");
                    q_cur->threadID_sender = pthread_self();
                    q_cur->numMiddleSend    = nextHopInfo->numMiddleSend; /* 送信スレッドでデバッグのために出力 */
                    if(pthread_create(&q_cur->threadID_sender, NULL, sendWaitAndResend, q_cur) != 0) {
                        err(EXIT_FAILURE, "sendWaitAndResend thread fail to create\n");
                    }
                    break;
                       
                }
            } else if ( nextHopInfo->typeARQ == ACK
                        && numSearch > DEL_PACKET_QUEUE_SEARCH_MAX) {
                /*ここシーケンス番号を判断してbreakしたいがシーケンスがオーバーフローして0になると制御できない*/

                break; 
            } else if (nextHopInfo->typeARQ == NACK
                       && numSearch > nextHopInfo->numPendinginCache) {
                /* pending要素分サーチしてパケットが見つからなければ再送しない(できない) */
                break;
            }
            q_next = q_cur;
            q_cur  = q_cur->prev;

            numSearch++;
        }
        pthread_mutex_unlock(&nextHopInfo->queueMutex);

        
        /*!-unlock-!*/
       
    } //infinute loop
}


/* 送信が終了したパケットをキューから削除する
 * @param param      ある1つの送信先に対応する(struct next_hop_addr_info型)ポインタ
 * u_short                   listNumber;    項目No
 * struct in_addr            ipDest;       パケット宛先IPアドレス                           
 * struct in_addr            ipMy;          自ノードIPアドレス                                
 * struct tx_packet          queueHead;     送信キューヘッド(next:末尾 prev:先頭以外に要素を入れない   
 * pthread_mutex_t           queueMutex;    キューの排他制御のmutex(実体)
 * u_short                   destPort;      宛先ポート                                     
 * u_short                   recvPort;     送信元ポート&ACK受信用ポート                     
 * u_int                     queueSize;     キューサイズ                                    
 * struct next_hop_addr_info *next;
 */
static void *delPacketFromQueue (void *param) {

    /*型変換用*/
    struct next_hop_addr_info *nextHopInfo;
    /*送信キューnext(curの一つ後方のポインタ),cur(操作対象のポインタ)とする*/
    struct tx_packet          *q_next, *q_cur;
    //キュー削除用シグナルを受信したら、キューから要素削除を開始する。
    //シーケンス番号に対応するキュー要素を探索・再送制御スレッド&キュー要素削除スレッドへのシグナル送信
    sigset_t  signal_set;
    int       signal_del_queue = SIGNAL_DEL_QUEUE;

    //debug param
    int      num_threads;
    u_int    pendinginCacheMax = TX_QUEUE_LENGTH_MAX/CACHE_MAX_THRESH; //NACKの場合はこのこの

    sigemptyset     ( &signal_set );
    sigaddset       ( &signal_set, signal_del_queue );   // dequeue
    pthread_sigmask ( SIG_BLOCK, &signal_set, 0 );

    /*型変換*/
    nextHopInfo = (struct next_hop_addr_info *)param;
    //スレッド存在のフラグ
    nextHopInfo->isThreadDQExists = THREAD_START;
    while(1) {
        //キュー要素削除シグナル受信までwait
        sigwait(&signal_set, &signal_del_queue);
      
        /*!-lock-!*/
        printf("[delPacketFromQueue ID %#x] Delete packet from queue signal received.\n"
               ,pthread_self());
        pthread_mutex_lock(&nextHopInfo->queueMutex); 
       
        q_next = &nextHopInfo->queueHead;
        for( q_cur = nextHopInfo->queueHead.prev; q_cur != &nextHopInfo->queueHead; ) { //キュー先頭から最後尾まで検索

            if(q_cur->isThreadExists_sender == THREAD_EXIT) { //スレッドの終了→項目削除開始
                
                //[要修正]sigwaitする度に同じ項目から見るため
                                
                /*NACK利用の場合は、pendingCacheMaxまではキャッシュする*/
                if(nextHopInfo->typeARQ != NACK || nextHopInfo->numPendinginCache >= pendinginCacheMax){
                    /* printf("[delPacketFromQueue ID %#x] del item  %d queueLen %d pending %d pendignMAX %d\n" */
                    /*        ,pthread_self() */
                    /*        ,q_cur->sequenceNumber */
                    /*        ,nextHopInfo->queueSize */
                    /*        ,nextHopInfo->numPendinginCache */
                    /*        , pendinginCacheMax); */
                    free(q_cur->packet);    //まずパケットの領域を解放
                    q_cur = q_cur->prev;    
                    free(q_next->prev);     //他の領域を解放
                    q_next->prev = q_cur;
                    q_cur->next  = q_next;
                    if(nextHopInfo->typeARQ == ACK) {
                        nextHopInfo->numMiddleSend--;  //送信処理中スレッド数
                    }
                    nextHopInfo->numPendinginCache--;  //pending
                    nextHopInfo->queueSize--;          //キューサイズ更新
                } else {/*ここからはデバッグなので、いらない*/
                    
                    q_next = q_cur; 
                    q_cur  = q_cur->prev;
                }
                
                            
            } else if (q_cur->isThreadExists_sender == THREAD_READY) {
                //未送信状態のパケットが発見(以降は未送信であるためチェック不要)
                break;
            } else { //ここには来ないだろうと思われる
                q_next = q_cur; 
                q_cur  = q_cur->prev;
            }
            
        }
        /*printf("[delPacketFromQueue ID %#x] Delete seq %d (now qsize %d, num middle of send %d\n"
               ,pthread_self()
               ,nextHopInfo->queueSize
               ,nextHopInfo->numMiddleSend
               );*/
        
        pthread_mutex_unlock(&nextHopInfo->queueMutex);
        /*!-unlock-!*/
        if(nextHopInfo->waitingDequeueSig == TRUE) {
            
            //削除終わりのため、送信処理スレッドへのシグナル送信
            if(pthread_kill(nextHopInfo->threadID_unicastWithARQ,signal_del_queue) != 0) { 
                err(EXIT_FAILURE, "fail to send signal ()\n");
            }
        }
        //デバッグ
        /*        printf("[delPacketFromQueue %#x] delete %d packets\n"
               ,pthread_self()
               ,num_threads);*/
    }
   
}

/* 
 * 再送制御ありでユニキャストする 
 * @param struct next_hop_addr_info *param
 *       u_short                   listNumber;    項目No
 *       pthread_t                 threadID;      キュー内要素送信スレッドのID 
 *       struct in_addr            ipDest;        パケット宛先IPアドレス                           
 *       struct in_addr            ipMy;          自ノードIPアドレス(ACK受信時に使う)               
 *       struct tx_packet          queueHead;     送信キューヘッド(next:末尾 prev:先頭以外に要素を入れない   
 *       pthread_mutex_t           queueMutex;    キューの排他制御のmutex(実体) 項目作成時にPTHREAD_MUTEX_INITIALIZER 
 *       u_short                   destPort;      宛先ポート                                     
 *       u_short                   recvPort;      送信元ポート&ACK受信用ポート                     
 *       u_int                     queueSize;     キューサイズ
 *       u_short                   nonBlocking;   ノンブロッキング送信時（画像データ送信時）、1 ブロッキング、0
 *       u_int                     windowSize;   /* ACKを待たずに一度に送信処理できるパケット数
 *     u_int                     numMiddleSend;   /* 現在同時送信処理しているパケット数 windoSize未満となる    
 *       struct tx_packet         *prev;          前へのポインタ
 *       struct tx_packet         *next;          次へのポインタ
 *
 * ACK受信用スレッド定義すること。このスレッドは１回のUnicastWithARQにつき１個だけ呼び出しし、なんらかのACKを受信したら
 * そのシーケンス番号に対応する再送制御スレッドをpthread_killでシグナル送る（受信したらmallocで確保したパケットのメモリをfreeしてreturnする）
 *
 * 終了条件；1度作られたら終了しない。キュー要素すべて送信処理し、ヘッドに戻ったらenqueueシグナルの待ち状態となる。
 */
static void *UnicastWithARQ (void *param) {

    /*通信用パラメータ*/
    int num_sent=0;                   /*送信パケットサイズ                       */
    int num_recv=0;                   /*受信パケットサイズ                       */
    int destSock;                     /* 送信用ソケットディスクリプタ             */
    int bindStatus;                   /* bindのステータス格納                    */
    struct sockaddr_in destAddr;      /* 宛先アドレス(送信用)                   */
    //    struct in_addr     destIP;        /* サーバのIPアドレス                 */
    u_short            destPort;      /* 宛先ポート                              */
    u_short            myPort;        /* ACK受付用ポート                         */
    u_short            packetSize;    

    /*送信パケット情報リスト*/
    struct next_hop_addr_info  *sPI_prev, *sPI_cur;
    /*送信キューprev(nextの一つ前のポインタ),next(操作対象のポインタ)とする*/
    struct tx_packet           *q_prev, *q_cur;
    /*引数の型変換*/
    struct next_hop_addr_info  *nextHopAddr = (struct next_hop_addr_info *)param;

    /*キュー内が空の状態でキューに新たなパケットがエンキューされたときに受信するシグナルの登録*/
    sigset_t    signal_set;
    sigset_t    signal_set_delQ;
    sigset_t    signal_set_sender;
    
    /* 次のシグナルの送信順番は
     *        1.送信完了or失敗
     *        2.キュー要素削除スレッドへSIG_DEL_QUEUE 、キュー要素削除
     *        3. 本スレッドへSIG_DEL_QUEUE とする
     */
    int         signal_enqueue           = SIGNAL_ENQUEUE;
    int         signal_port_open         = SIGNAL_PORT_OPEN;
    int         signal_del_queue         = SIGNAL_DEL_QUEUE;
    int         signal_send_one_packet   = SIGNAL_SEND_ONE_PACKET;
    
    /* sigwaitでの受信シグナル */
    int         recv_sig;
    
    pthread_t   recvACK_ID;     //ACK待ちスレッドのID
    //画像データ用バッファサイズ (ノンブロッキングソケット設定用)
    int         socket_buffer_size_image = SOCKET_BUFFER_SIZE_IMAGE;
    int         val = 1;/* ioctl でノンブロッキングする場合*/
    //setsock status
    u_char      low_delay;
    int         setsock_status;
    
    /*TCP送信時*/
    int         numSendBytes; 
    int         connectStatus;
    int         numsnt;
    
    /*シグナルの登録(queue)*/
    sigemptyset     ( &signal_set );
    sigaddset       ( &signal_set, signal_port_open );      // port open
    sigaddset       ( &signal_set, signal_enqueue );        // enqueue
    pthread_sigmask ( SIG_BLOCK, &signal_set, 0 );
    
    sigemptyset     ( &signal_set_delQ );
    sigaddset       ( &signal_set_delQ, signal_del_queue ); /* キュー要素削除イベント */
    pthread_sigmask ( SIG_BLOCK, &signal_set_delQ, 0 );

    sigemptyset     ( &signal_set_sender );
    sigaddset       ( &signal_set_sender, signal_send_one_packet); // １回目の送信はシグナルを待たずにcreate
    pthread_sigmask ( SIG_BLOCK, &signal_set_sender, 0 );

    pthread_kill(pthread_self(), signal_send_one_packet);//１回目の送信のため
    
    //スレッド存在中のフラグ
    nextHopAddr->isThreadUWAExists = THREAD_START;
    printf("[UnicastWithARQ %#x] called protocol type %d (TCP %d UDP %d) (this thread is for  %s port %d.\n"
           ,pthread_self()
           ,nextHopAddr->protocolType
           ,TCP
           ,UDP
           ,inet_ntoa(nextHopAddr->ipDest)
           ,nextHopAddr->destPort);
  
    if(nextHopAddr->protocolType == UDP) {

        /*APPレベルでACK受信&再送するときはACK待ちスレッド作る*/
       
        /* 再送用スレッドの起動 */
        /* if( nextHopAddr->isThreadRSExists == THREAD_READY */
        /*     && pthread_create ( &nextHopAddr->threadID_resender, */
        /*                         NULL, packetResender, nextHopAddr) != 0 ){ */
        /*     err (EXIT_FAILURE, "packetResender thread fail to create\n"); */
        /* } */
        /* nextHopAddr->isThreadRSExists = THREAD_START; */

        /*ACK待ちスレッドを起動. ACKを受信したら対応するシーケンスナンバのキュー要素を削除する*/
        if( nextHopAddr->isThreadRACKExists == THREAD_READY
            && pthread_create ( &nextHopAddr->threadID_recvACKorNACK,
                                NULL, recvACKorNACK, nextHopAddr) != 0 ){
            err (EXIT_FAILURE, "recvACKorNACK thread fail to create\n");
        }
        nextHopAddr->isThreadRACKExists = THREAD_START;
        
        /*パケットをキューから削除するスレッドの起動*/
        if( nextHopAddr->isThreadDQExists == THREAD_READY
            && pthread_create ( &nextHopAddr->threadID_delQueue,
                                NULL, delPacketFromQueue, nextHopAddr) != 0) {
            err(EXIT_FAILURE, "delPacketFromQueue thread fail to create\n");
        }
        nextHopAddr->isThreadDQExists = THREAD_START;
       

        sigwait(&signal_set, &recv_sig); // ack受信用ポートのバインドが完了)
        printf("[UnicastWithARQ %#x] received signal port open. now socket for send creation\n",pthread_self());
    
        /* (送信用)UDPデータグラムソケットの作成 */ 
        if ((nextHopAddr->destSock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
            err(EXIT_FAILURE,"socket() failed\n");
        }

        //制御メッセージを送信する場合ははIPヘッダのTOSでlow delayにする（効果あるかよくわからないが）
        if(   nextHopAddr->packetType == AMOUNT_DATA_SEND_MESS
           || nextHopAddr->packetType == INITIAL_PERIOD_MESS
           || nextHopAddr->packetType == NEW_PERIOD_MESS
           || nextHopAddr->packetType == EXIT_CODE) {
            low_delay = 46 << 2;
            setsock_status = setsockopt(nextHopAddr->destSock, SOL_IP ,IP_TOS,  &low_delay, sizeof(low_delay));
        }
        /*送信用アドレス構造体の定義*/
        memset(&destAddr, 0, sizeof(destAddr));
        destAddr.sin_family = AF_INET;
        destAddr.sin_addr   = nextHopAddr->ipDest;
        destAddr.sin_port   = htons(nextHopAddr->destPort);

        //ノンブロッキング指定(画像のみ)の場合、ソケットをノンブロッキングへ（バッファ溢れが起きてもsendがブロックしない
        /* ARQ利用の場合、キューイング時にあふれたパケットをロスさせ、
         * ここでは確実に送信することで、シーケンス抜けを防止する */
        if(nextHopAddr->nonBlocking && (IMAGE_DATA_TRANSFER_WITH_HOPBYHOP_ARQ==0)) {
            ioctl(nextHopAddr->destSock, FIONBIO, &val);
        }
        
        /* パケットのユニキャスト */
        /* [注意]エンキューシグナル、デキューシグナル、送信許可シグナルの待ち状態になる */
        while(1) {
            /*!-lock-!*/
            printf("[UnicastWithARQ %#x] wait for unlock mutex\n",pthread_self());

            pthread_mutex_lock  (&nextHopAddr->queueMutex);
            q_prev = &nextHopAddr->queueHead; //キュー先頭から(prevからキュー先頭を示す)
            q_cur  = nextHopAddr->queueHead.prev;
                        
            while (q_cur != NULL && q_cur != &nextHopAddr->queueHead  ) {
             

                if(q_cur->isThreadExists_sender==THREAD_READY) { //送信スレッド起動前についてのみ

                    /* 同時送信処理数<=WINDOW_SIZEのときは送信処理を待機*/
                    
                    q_cur->destSock      = &nextHopAddr->destSock; //ソケットの参照
                    q_cur->destAddr      = destAddr;               //送信先アドレス情報

                    if(nextHopAddr->numMiddleSend > WINDOW_SIZE) {  //送信処理パケット数==ウインドウサイズ上限

                        /*デキュー待ち */
                        pthread_mutex_unlock(&nextHopAddr->queueMutex);
                        nextHopAddr->waitingDequeueSig = TRUE;
                        do {
                            sigwait(&signal_set_delQ, &recv_sig);
                        } while (recv_sig != signal_del_queue);
                        nextHopAddr->waitingDequeueSig = FALSE;
                        pthread_mutex_lock(&nextHopAddr->queueMutex);
                    }

                    
                    //スレッドは作成順に実行される保証がないため、
                    //前回送信したパケットがsendされた時のシグナルを待つ(第一回目は自身が送る)

                    /*!-unlock-!*/
                    pthread_mutex_unlock  (&nextHopAddr->queueMutex);
                    do {
                        sigwait(&signal_set_sender, &recv_sig);
                        if (recv_sig != signal_send_one_packet) {
                            printf("signal %d is not the one i waiting for(%d).\n"
                                   ,recv_sig
                                   ,signal_send_one_packet);
                        }
                    } while(recv_sig != signal_send_one_packet);
                    
                    printf("[UnicastWithARQ %#x] received signal send one pac. now thread created\n",pthread_self());

                    /*!-lock-!*/
                    
                    pthread_mutex_lock(&nextHopAddr->queueMutex);

                    /* printf("[UnicatWithARQ] received enable thread create signal\n"); */
                    q_cur->threadID_createSender = pthread_self(); //このスレッドのID
                    q_cur->numMiddleSend    = nextHopAddr->numMiddleSend; /* 送信スレッドでデバッグのために出力 */
                    q_cur->isThreadExists_sender = THREAD_START; 
                    /*キュー要素送信処理スレッド作成*/
                    if(pthread_create(&q_cur->threadID_sender, NULL, sendWaitAndResend, q_cur) != 0) {
                        err(EXIT_FAILURE, "sendWaitAndResend thread fail to create\n");
                    }
                    /*                printf("[UnicastWithARQ %#x] sender thread id %#x is created\n"
                                      ,pthread_self()
                                      ,q_cur->threadID_sender);
                    */

                    if(nextHopAddr->typeARQ == ACK) {
                        nextHopAddr->numMiddleSend++;     //送信処理中スレッド数(NACK)
                    }
                    nextHopAddr->numPendinginCache++; //送信処理済みパケット数
                    
                }

                q_cur = q_cur->prev;


            }
            pthread_mutex_unlock(&nextHopAddr->queueMutex);
            /*!-unlock-!*/
            /* printf("[UnicastWithARQ] for LOOP end. Waiting for next  enqueue signal  (now qsize %d now middle of send %d).\n"
               , nextHopAddr->queueSize
               , nextHopAddr->numMiddleSend);*/
            /*エンキュー待ち*/
       
            nextHopAddr->waitingEnqueueSig = TRUE;
            do {
                sigwait(&signal_set, &recv_sig);
            } while (recv_sig != signal_enqueue);; //キューが空になったら SIGNAL_ENQUEUEを待つ
            //printf("[UnicastWithARQ %#x] enq signal received. ready to create sender thread.\n",pthread_self());
            nextHopAddr->waitingEnqueueSig = FALSE;
        }
        
    } else if (nextHopAddr->protocolType == TCP) { /*TCP利用時(基本的に制御メッセージのみTCPを使用する)*/

        //TCPの３ウェイ
        /* (送信用)TCPソケットの作成 */ 
        if ((nextHopAddr->destSock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
            err(EXIT_FAILURE,"socket() failed\n");
        }
        /* ACK送信時にの時に有効になるか確かめる */
        low_delay = 46 << 2;
        setsockopt(nextHopAddr->destSock, SOL_IP ,IP_TOS,  &low_delay, sizeof(low_delay));

        /*送信用アドレス構造体の定義*/
        memset(&destAddr, 0, sizeof(destAddr));
        destAddr.sin_family = AF_INET;
        destAddr.sin_addr   = nextHopAddr->ipDest;
        destAddr.sin_port   = htons(nextHopAddr->destPort);
        printf("[UnicastWithARQ %#x] try connect to %s port %d.\n"
               ,pthread_self()
               ,inet_ntoa(nextHopAddr->ipDest)
               ,nextHopAddr->destPort);
        while ((connectStatus  = connect(nextHopAddr->destSock
                                         ,(struct sockaddr *) &destAddr
                                         ,sizeof(destAddr))) < 0)
            ; //コネクトが成功するまでループ
        
        printf("[UnicastWithARQ %#x] suc connect to %s port %d.\n"
               ,pthread_self()
               ,inet_ntoa(nextHopAddr->ipDest)
               ,nextHopAddr->destPort);
        /* パケット送信 */
        while(1) {
            /*!-lock-!*/
            pthread_mutex_lock  (&nextHopAddr->queueMutex);
            q_prev = &nextHopAddr->queueHead; //キュー先頭から(prevからキュー先頭を示す)
            q_cur  = nextHopAddr->queueHead.prev;
            pthread_mutex_unlock(&nextHopAddr->queueMutex);
            /*!-unlock-!*/

            while (q_cur != NULL && q_cur != &nextHopAddr->queueHead  ) {
             
                //sendは失敗するとSIGPIPE出すので、オプションでシグナルを発生させない
                //-1:コネクション切断、0:無事に送信(下位バッファに入るだけだが)
                while((numsnt = send(nextHopAddr->destSock,q_cur->packet , q_cur->packetSize, MSG_NOSIGNAL)) < 0) {
                    
                    /*[コネクションロス等による失敗の場合、コネクトしなおす]送信用アドレス構造体初期化しなおし*/
                    memset(&destAddr, 0, sizeof(destAddr));
                    destAddr.sin_family = AF_INET;
                    destAddr.sin_addr   = nextHopAddr->ipDest;
                    destAddr.sin_port   = htons(nextHopAddr->destPort);
                    /* close(dstSocket); */
                    if ((nextHopAddr->destSock = socket(AF_INET, SOCK_STREAM, 0)) < 0) { 
                        err(EXIT_FAILURE, "fail to create socoket\n"); 
                    }
                    /* ACK送信時にの時に有効になるか確かめる */
                    low_delay = 46 << 2;
                    setsockopt(nextHopAddr->destSock, SOL_IP ,IP_TOS,  &low_delay, sizeof(low_delay));
                    while ((connectStatus= connect(nextHopAddr->destSock
                                                   ,(struct sockaddr *) &destAddr
                                                   ,sizeof(destAddr))) < 0)
                        ; //コネクト
                    
                }
                /*!-lock-!*/
                pthread_mutex_lock  (&nextHopAddr->queueMutex);
                free(q_cur->packet);    //まずパケットの領域を解放
                q_cur = q_cur->prev;    
                free(q_prev->prev);     //他の領域を解放
                q_prev->prev = q_cur;
                q_cur->next  = q_prev;
                nextHopAddr->queueSize--;//キューサイズ更新
                pthread_mutex_unlock(&nextHopAddr->queueMutex);
                /*!-unlock-!*/

            }
            /* printf("[UnicastWithARQ] for LOOP end. Waiting for next  enqueue signal  (now qsize %d now middle of send %d).\n"
               , nextHopAddr->queueSize
               , nextHopAddr->numMiddleSend);*/
            /*すべて送信終了したら、エンキュー待ち*/
            nextHopAddr->waitingEnqueueSig = TRUE;
            do {
                sigwait(&signal_set, &recv_sig); //キューが空になったら SIGNAL_ENQUEUEを待つ
            } while (recv_sig != signal_enqueue);
            
            //printf("[UnicastWithARQ %#x] enq signal received. ready to create sender thread.\n",pthread_self());
            nextHopAddr->waitingEnqueueSig = FALSE;
        }
        
    }
}




/* エンキューしたら対応する送信スレッドIDにシグナルを送信する                                */
/* 引数にて取得したパケットをキュー要素として領域確保後、シーケンス番号＋パケット内容としてキュー要素にコピー
 * 隣接ノードリストのキューサイズが0であれば エンキューした要素にキュー先頭ポインタとするキュー要素のnextをヘッドにする
 * 隣接ノードリスト項目内のキューにエンキューし、送信用スレッドをcreateする
 *
 *
 * @param  struct sendingPacketInfo  (パケット本体をコピーするだけ)
 *      u_short        nextNodeType;     次ホップが子ノード:CHILDLEN, 親ノード:PARENT   
 *      u_short        packetType;       送信パケットタイプ(下部enum内の中から指定) 
 *      struct in_addr ipAddr;       CHILEDLEN(親ノードのIPv4アドレスを指定) PARENT(画像収集ノードのIP) 
 *      u_short        destPort;         パケット送信の宛先ポート                
 *      u_short        dataReceivePort;  （中継時に指定）パケット受信用ポート     
 *      u_short        ackPortMin;       ACK受信用ポート最小値                  
 *      u_short        ackPortMax;       ACK受信用ポート最大値                  
 *      char          *packet;          送信するパケット                       
 *      u_short        packetSize;       送信パケットサイズ
 *
 *  @param struct next_hop_addr_info *      次ホップノード情報の項目１つを与える[nextで他の要素へのアクセスしないこと]
 *                                         
 *      struct sendingPacketInfo
 *      u_short        isChildlenNext;     次ホップが子ノード（複数可）0:NO, 1:YES 
 *      u_short        isParentNext;       次ホップが親ノード        0:NO   1;YES 
 *      struct in_addr rootIP;             画像収集ノードIP                       
 *      u_short        destPort;           パケット送信の宛先ポート
 *      u_short        dataReceivePort;    （中継時に指定）パケット受信用ポート    
 *      u_short        ackPortMin;         ACK受信用ポート最小値                  
 *      u_short        ackPortMax;         ACK受信用ポート最大値                 
 *      char          *packet;             送信するパケット                     
 *      u_short        packet_size;         送信パケットサイズ                   
 */

static void UnicastPacket(struct sendingPacketInfo sPInfo, struct next_hop_addr_info *nextHopAddrInfo)  {

    /*宛先アドレス構造体*/
    struct sockaddr_in     servAddr;

    /*送信用キュー (項目が送信パケット、それらを連結したリストがキューとなる)*/
    struct  tx_packet     *txpacket_next, *txpacket_new;
        
    /*UnicastWithARQスレッドの再起動用シグナル設定*/
    sigset_t  signal_set;
    int       signal_enqueue = SIGNAL_ENQUEUE;

    u_short  first_pac = FIRST_PAC;
    //本スレッドが送信シグナルをブロック登録。デフォルト動作しないようにする(デフォルト動作は終了)
    sigemptyset(&signal_set); //
    sigaddset(&signal_set, signal_enqueue);//
    sigprocmask( SIG_BLOCK, &signal_set, 0);//
    
   
    /* [注意]シーケンスナンバの更新は、エンキューが成功してから行う */

    
    if(nextHopAddrInfo->queueSize < TX_QUEUE_LENGTH_MAX) {
        /* パケットのエンキュー*/
        if ((txpacket_new = (struct tx_packet *)malloc(sizeof(struct tx_packet))) == NULL ) {
            err(EXIT_FAILURE,"fail to allcate memory UnicastPacket txpacket_new\n");
        }
    
        /*要素内容の初期化*/
        if(sPInfo.packetType == IMAGE_DATA_PACKET && IMAGE_DATA_TRANSFER_WITH_HOPBYHOP_NACK_ARQ) {
            txpacket_new->packetSize = sPInfo.packetSize + NACKBASED_ARQ_HEADER_SIZE; //orig packet size + sequence number + port for ACK.
        } else if (sPInfo.protocolType == TCP) {
            txpacket_new->packetSize = sPInfo.packetSize; //orig packet size.
        } else {
            txpacket_new->packetSize = sPInfo.packetSize + ARQ_HEADER_SIZE;
        }
    
        //シーケンスナンバの更新(中継時にも同時にアクセスされるので、mutex_lock & unlockしておく)
        if((txpacket_new->packet  = (char *)malloc(sizeof(char)*txpacket_new->packetSize)) == NULL) {
            err(EXIT_FAILURE,"fail to allcate memory UnicastPacket txpacket_new->packet\n");
        }
            
        /*!-lock-!*/
        /* パケットの作成 */
        pthread_mutex_lock(&SequenceNumberMutex[sPInfo.packetType]);
        txpacket_new->sequenceNumber = SequenceNumber[sPInfo.packetType];
        SequenceNumber[sPInfo.packetType]++;
        pthread_mutex_unlock(&SequenceNumberMutex[sPInfo.packetType]);
        /*!-unlock-!*/

        /*!-lock-!*/
        pthread_mutex_lock(&nextHopAddrInfo->queueMutex);
        memcpy(txpacket_new->packet,                   &nextHopAddrInfo->recvPort,    sizeof(u_short));   //waitin ACK port 
        memcpy(txpacket_new->packet+sizeof(u_short),   &txpacket_new->sequenceNumber, sizeof(u_short));   //sequence number

        if(sPInfo.packetType == IMAGE_DATA_PACKET && IMAGE_DATA_TRANSFER_WITH_HOPBYHOP_NACK_ARQ) {
            memcpy(txpacket_new->packet+2*sizeof(u_short), &first_pac ,  sizeof(u_short)); //using nack
        
            memcpy(txpacket_new->packet+3*sizeof(u_short),  sPInfo.packet,  sPInfo.packetSize); //application data

        } else if (sPInfo.protocolType == TCP){
            memcpy(txpacket_new->packet,  sPInfo.packet,  sPInfo.packetSize); //application data
        } else {
            memcpy(txpacket_new->packet+2*sizeof(u_short),  sPInfo.packet,  sPInfo.packetSize); //application data
        }
        txpacket_new->queueMutex              = &nextHopAddrInfo->queueMutex;
        txpacket_new->threadID_delQueue       = &nextHopAddrInfo->threadID_delQueue;
        txpacket_new->isThreadExists_sender    = THREAD_READY;  //送信スレッド未作成の状態

        if(nextHopAddrInfo->typeARQ == NACK) {
            txpacket_new->timerResend              = 0.0; //ここは意味ない
            txpacket_new->maxResend                = 0;//ここで0以外指定しない
        } else if (nextHopAddrInfo->typeARQ == ACK) {
            txpacket_new->timerResend              = ACK_TIMEOUT;
            txpacket_new->maxResend                = ACK_RETRY_MAX;
        }
    
        /*threadIDはthread create時に初期化*/
        /*リストに連結 (キュー内に要素がある場合とない場合で連結処理が異なることに注意)*/
        if(nextHopAddrInfo->queueSize == 0) { /* キュー内にパケットなし */
            //        printf("[UnicastPacket %#x] enqueue (queue is empty)\n",pthread_self());
            nextHopAddrInfo->queueHead.prev            = txpacket_new;   //キュー先頭(送信が先に行われる方)
            nextHopAddrInfo->queueHead.next            = txpacket_new;   //キュー終端(この箇所はデキュー時に再設定する)
            nextHopAddrInfo->queueHead.isThreadExists_sender
                = nextHopAddrInfo->queueHead.isThreadExists_delQueue 
                = THREAD_NON;
                
            txpacket_new->next               = &nextHopAddrInfo->queueHead;    //送信パケット項目の次、前
            txpacket_new->prev               = &nextHopAddrInfo->queueHead;
            nextHopAddrInfo->queueSize       = 1; //キュー長さ
        
            if(nextHopAddrInfo->isThreadUWAExists == THREAD_READY) { //UnicastWithARQスレッドがない場合
          
                //パケット送信用スレッド起動
                if(  pthread_create(&nextHopAddrInfo->threadID_unicastWithARQ ,NULL ,UnicastWithARQ , nextHopAddrInfo) != 0) {
                    err (EXIT_FAILURE, "thread for ARQ fail to create (UnicastPacket)\n");
                }
                    
            } else {
                printf("thread already exist for dest ip %s port %d. (sendThread %#x) (Queue size (%d) middle of sent (%d))\n"
                       ,inet_ntoa(nextHopAddrInfo->ipDest)
                       ,nextHopAddrInfo->destPort
                       ,nextHopAddrInfo->threadID_unicastWithARQ
                       ,nextHopAddrInfo->queueSize
                       ,nextHopAddrInfo->numMiddleSend
                );
            }
               
        } else if (nextHopAddrInfo->queueSize > 0) {/* すでに挿入済みのパケットがある */
            txpacket_next                             = nextHopAddrInfo->queueHead.next; //キューの上から2番目のパケット（退避）
            /* ポインタ付け替え */
            nextHopAddrInfo->queueHead.next           = txpacket_new;                    //新要素をキュー最後尾に
            txpacket_new->prev                        = &nextHopAddrInfo->queueHead;     //新要素の前をキューヘッドに
            txpacket_next->prev                       = txpacket_new;
            txpacket_new->next                        =  txpacket_next;

            nextHopAddrInfo->queueSize++;
                                    
        }
        printf("successfully enqueue for dest ip %s port %d. seq %d (Queue size (%d) middle of sent (%d))\n"
               ,inet_ntoa(nextHopAddrInfo->ipDest)
               ,nextHopAddrInfo->destPort
               ,txpacket_new->sequenceNumber
               ,nextHopAddrInfo->queueSize
               ,nextHopAddrInfo->numMiddleSend);
        pthread_mutex_unlock(&nextHopAddrInfo->queueMutex); 
        /*!-unlock-!*/
    } else { /* キューサイズが TX_QUEUE_LENGTH_MAX以上になるとエンキューせず、領域確保しない*/
       
        printf("fail to  enqueue for dest ip %s port %d.  (Queue size (%d) middle of sent (%d))\n"
               ,inet_ntoa(nextHopAddrInfo->ipDest)
               ,nextHopAddrInfo->destPort
               ,nextHopAddrInfo->queueSize
               ,nextHopAddrInfo->numMiddleSend);
    }
   
   
    
    //Enqueue待ち状態ならばシグナルを送信
    if ( nextHopAddrInfo->isThreadUWAExists == THREAD_START && nextHopAddrInfo->waitingEnqueueSig == TRUE) { 
        if(pthread_kill(nextHopAddrInfo->threadID_unicastWithARQ, signal_enqueue) != 0) {
            err(EXIT_FAILURE, "fail to send signal (UnicastPacket)\n");
        }
    }
    

    
}


/*  子ノード ot 親ノードにパケットを送信する
 *  @param struct sendingPacketInfo
 *  u_short        nextNodeType;     次ホップが子ノード:CHILDLEN, 親ノード:PARENT   
 *  u_short        packetType;       送信パケットタイプ(下部enum内の中から指定) 
 *  struct in_addr ipAddr;           CHILEDLEN(親ノードのIPv4アドレスを指定 親がいなければ割り振られていないIPアドレス入れる) PARENT(画像収集ノードのIP) 
 *  u_short        destPort;         パケット送信の宛先ポート                
 *  u_short        dataReceivePort; （中継時に指定）パケット受信用ポート     
 *  u_short        ackPortMin;       ACK受信用ポート最小値                  
 *  u_short        ackPortMax;       ACK受信用ポート最大値                  
 *  char          *packet;           送信するパケット                       
 *  u_short        packetSize;       送信パケットサイズ
 */

#define WLAN_NET  (50)
#define LAN_NET  (30)

void sendPacketToNext (struct sendingPacketInfo sPInfo) {

    /*要素番号*/
    u_short                    nexthopl_listnum = 0;

    /*隣接ノード情報リスト、ヘッド（ファイル先頭部分でstatic宣言したヘッドのどれかを指す*/
    struct next_hop_addr_info *nexthop_list_head;
    /*リスト探索用 (prev 1つ前, cur 現在 new 新規挿入用 proc 操作対象*/
    struct next_hop_addr_info *nexthopl_prev, *nexthopl_cur, *nexthopl_new, *nexthopl_proc;
    u_short                    nexthopl_hit = 0; //隣接ノードリスト探索時に発見した場合、1
        
    /*/proc/net/route参照による親ノード取得用*/
    char                       get_route_command[100];
    char                      *get_route_script = GET_NEXT_HOP_SCRIPT; //次ホップ取得用スクリプト    
    char                      *route_filename;
    FILE                      *route_fp;
    short                      one_line_max     = 20; //１行の最大サイズ(IPV4アドレスがドット表記ですべて収まればよい)
    char                       one_addr  [one_line_max];   //読み込んだファイル内アドレス
    u_short                    num_ack_recv_port;
    /*次ホップIPアドレスリスト ヒープ領域に確保したメモリはこの関数内で必ずfreeする*/
    struct nextAddr { 
        struct in_addr   ipAddr;
        struct nextAddr *next;
    } addrListHead, *addrPrev, *addrCur, *addrNew;
    
    /* ブロードキャスト用ソケット */
    int                 broadcastSock;
    char                *servIP;
    struct sockaddr_in  broadcastAddr;
    int                 True = 1;
    int                 sent_size;
    /* IPアドレス置換用コマンド 置換されたIPアドレスが入るファイル名 */
    char replace_command[100];
    char *replace_ip_file = "onehop_lan_ip.dat";
    
    /* /\*ブロードキャスト*\/ */
    /* if( (sPInfo.packetType == INITIAL_PERIOD_MESS || sPInfo.packetType == NEW_PERIOD_MESS || sPInfo.packetType == EXIT_CODE)  */
    /*     && CONTROL_METHOD_BROADCAST) {        */
    /*     if ((broadcastSock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) */
    /*         DieWithError("socket() failed"); */
        
    /*     setsockopt(broadcastSock, SOL_SOCKET, SO_REUSEADDR, &True, sizeof(int)); /\*ソケットの再利用*\/ */
    /*     if (bind(broadcastSock,(struct sockaddr *) &broadcastAddr, sizeof(broadcastAddr)) < 0) */
    /*         err(EXIT_FAILURE, "error while bind source port (SendingACK)\n"); */

        
    /*     memset(&broadcastAddr, 0, sizeof(broadcastAddr));    */
    /*     servIP                        = BROADCAST_IP;  /\*(無線)ブロードキャストIP宛*\/ */
    /*     broadcastAddr.sin_family      = AF_INET;                             */
    /*     broadcastAddr.sin_addr.s_addr = inet_addr(servIP);  */
    /*     broadcastAddr.sin_port        = htons(sPInfo.destPort);       */
        
    /*     setsockopt(broadcastSock, SOL_SOCKET, SO_BROADCAST, (char *)&True, sizeof(True)); */
    /*     printf("[Broadcast message\n]"); */
    /*     sent_size = sendto(broadcastSock, sPInfo.packet, sPInfo.packetSize, 0, (struct sockaddr *)&broadcastAddr, sizeof(broadcastAddr)); */
      
    /*     close(broadcastSock); */

    /* } else {    /\*ユニキャスト*\/ */

    /* 次ホップリスト更新処理、修正すべき点*/
    /* 1. 1つのアプリケーションデータ(フラグメントする場合も含む)が発生したら次ホップリストチェック＆更新とするべき。  */
    /*    フラグメントに対しても次ホップを更新する必要はない                                                       */
    /* [ひとまず] 実験ネットワークはトポロジが変化しないので、１度次ホップリストを更新したら、以後のコールでは更新しない*/
    /*                                                                                                         */
    /* 新規コール */
    if (Next_Hop_Addr_Info_Head[sPInfo.packetType].next == NULL) {
        //今後隣接ノードが変更されるなどして消去が起こる場合、直接的な条件に変更

        /* 宛先IPアドレスをCHILDLEN or PARENTかで決定。有線IFで確実に送信する場合は、有線IPに置換 */

        
        if(FOR_INDOOR_EXPERIMENT
           && (sPInfo.packetType==INITIAL_PERIOD_MESS || sPInfo.packetType==EXIT_CODE)) {
            sPInfo.ipAddr = changeIP(sPInfo.ipAddr,LAN_NET,WLAN_NET); /* IPアドレスを置換 */
        }
       
        
        /* 次ホップノード情報リスト内のキューに追加 */
        if ( sPInfo.nextNodeType == CHILDLEN ) { /*次ホップが子ノード*/
            route_filename = ONEHOP_CHILDREN;
            sprintf(get_route_command, "%s %s -c %s > %s"
                    , get_route_script, WIRELESS_DEVICE, inet_ntoa(sPInfo.ipAddr), route_filename);
            
        }else if ( sPInfo.nextNodeType == PARENT ) {
            route_filename = ONEHOP_PARENT;
            sprintf(get_route_command, "%s %s -p %s > %s"
                    , get_route_script, WIRELESS_DEVICE,inet_ntoa(sPInfo.ipAddr), route_filename);
        }

        
        /*!-lock-!*/
        pthread_mutex_lock(&route_file_read_write_mutex);
        printf("%s\n",get_route_command);
        system (get_route_command); //書き出し

        /*  室内実験の場合、有線IF用IPアドレスに置換して送信
           (ブロードキャストでは同じ有線ネットワークで複数の実験の制御ができないので、ユニキャストする) */
        if(FOR_INDOOR_EXPERIMENT
           && (sPInfo.packetType==INITIAL_PERIOD_MESS || sPInfo.packetType==EXIT_CODE)) {
               sprintf(replace_command, "sed -e 's/%d/%d/g' %s > %s"
                       ,WLAN_NET, LAN_NET, route_filename, replace_ip_file);
               system(replace_command);
               route_filename = replace_ip_file; /* 以後の操作対象のファイル名を変更 */
        }

        
        if((route_fp = fopen(route_filename, "r")) == NULL) {
            err(EXIT_FAILURE, "fail to open route file sendPacketToNext route_fp\n");
        }
        addrListHead.next = NULL;
        addrCur           = &addrListHead;
        /*ファイルから次ホップIPアドレス探索*/
        while((fgets(one_addr, one_line_max-1, route_fp )) != NULL) {
            one_addr[strlen(one_addr)-1] = '\0'; /*改行コード->\0に*/
            /* printf("[sendPacketToNext %#x] new next hop %s\n",pthread_self(), one_addr); */
            addrNew       = (struct nextAddr *)malloc( sizeof (struct nextAddr) );
            addrNew->next = NULL;
           
            if (inet_aton( one_addr, &addrNew->ipAddr) == 0) {
                err(EXIT_FAILURE, "inet_aton detect wrong address\n");
            }
            addrCur->next  = addrNew; //追加
            addrCur        = addrCur->next;

        }
        fclose(route_fp);
        pthread_mutex_unlock(&route_file_read_write_mutex);
        /*!-unlock-!*/
                        
        /*次ホップの決定、(次ホップリスト情報,スレッド作成) パケットのエンキュー*/
        addrPrev = &addrListHead;
        for(addrCur = addrListHead.next; addrCur != NULL; ) {
           
            /*次ホップリスト探索 項目になければ作成*/
            nexthopl_prev = &Next_Hop_Addr_Info_Head[sPInfo.packetType];
            nexthopl_listnum = 0;
            /*            printf("nexthopl_prev %p\n",
                          nexthopl_prev);*/
            for ( nexthopl_listnum = 0, nexthopl_cur = Next_Hop_Addr_Info_Head[sPInfo.packetType].next;
                  nexthopl_cur != NULL;
                  nexthopl_listnum++, nexthopl_cur = nexthopl_cur->next ) {
                
                if ( memcmp(&nexthopl_cur->ipDest, &addrCur->ipAddr, sizeof(struct in_addr)) == 0)  {
                    nexthopl_hit = 1; //ヒット->1 ヒットなし->0
                
                    break;
                }
                nexthopl_prev =  nexthopl_cur;
                
                
            }
        
            if(nexthopl_hit == 0) { //項目新規作成
                if( (nexthopl_new = (struct next_hop_addr_info  *)malloc(sizeof(struct next_hop_addr_info))) == NULL ) {
                    err(EXIT_FAILURE, "memory allocation failure [new_buffer_data]\n");
                }

                nexthopl_new->listNumber      = nexthopl_listnum;    /* リスト項目NO(ACK受信用ポート設定に用いるだけで,意味はない) */
                
                //再送ポリシ
                if(IMAGE_DATA_TRANSFER_WITH_HOPBYHOP_NACK_ARQ && sPInfo.packetType == IMAGE_DATA_PACKET) {
                    nexthopl_new->typeARQ = NACK; /*もしも他のパケットタイプでもNACK使用なのであれば、以下にelse if を追記*/
                } else {
                    nexthopl_new->typeARQ =  ACK; //基本的にはACKを利用
                }
                nexthopl_new->ipDest            = addrCur->ipAddr;     /* 宛先ノードIP、ポート                                     */
                nexthopl_new->destPort          = sPInfo.destPort;     /* 宛先ポート番号 */
                nexthopl_new->windowSize        = sPInfo.windowSize;   /*ウインドウサイズ*/
                nexthopl_new->numMiddleSend     = 0; //送信処理中スレッド数
                nexthopl_new->numPendinginCache = 0; //送信が終わって領域が解放されずに残っているパケット数(≠キューサイズ)
                nexthopl_new->queueMutex      = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
                nexthopl_new->ipMy            = GetMyIpAddr(WIRELESS_DEVICE);
                /*シグナル待ち状態のフラグ初期化*/
                nexthopl_new->waitingEnqueueSig = FALSE;
                nexthopl_new->waitingDequeueSig = FALSE;
                

                nexthopl_new->protocolType    = sPInfo.protocolType;
                nexthopl_new->packetType      = sPInfo.packetType;
                nexthopl_new->queueHead.next  = NULL;
                nexthopl_new->queueHead.prev  = NULL;
                nexthopl_new->queueSize       = 0; //キューサイズ
                /* スレッドコールの状態(すべて初期化すること) */
                nexthopl_new->isThreadUWAExists
                    = nexthopl_new->isThreadRACKExists
                    = nexthopl_new->isThreadRSExists
                    = nexthopl_new->isThreadDQExists
                    = THREAD_READY;
                nexthopl_prev->next           = nexthopl_new; //次ホップリストに連結
                nexthopl_new->next            = NULL;
                
                if( (nexthopl_new->recvPort = sPInfo.ackPortMin + nexthopl_listnum) > sPInfo.ackPortMax ) {
                    err(EXIT_FAILURE, "port(%d) for receive data exceed Maximum port number(%d) (sendPacketToNext)\n"
                        , nexthopl_new->recvPort
                        , sPInfo.ackPortMax);
                }
               
                if( sPInfo.packetType == IMAGE_DATA_PACKET) { //画像データパケットの場合はノンブロッキングソケット(送信)
                    nexthopl_new->nonBlocking = 1;
                }
                
                printf("[sendPacketToNext]new neighbor created. ipDest %s, destPort %d, sPInfo.packetType %d\n",
                       inet_ntoa(nexthopl_new->ipDest),
                       nexthopl_new->destPort,
                       sPInfo.packetType
                );
                /*パケットをキューに入れて送信処理*/
                UnicastPacket(sPInfo, nexthopl_new);
                
            } else {

                UnicastPacket(sPInfo, nexthopl_cur);
            }
            /*アドレスリストとして確保したヒープ領域の解放*/
            addrCur = addrCur->next;
            free(addrPrev->next);
            addrPrev->next = addrCur;
        }
    } else if (Next_Hop_Addr_Info_Head[sPInfo.packetType].next != NULL ) {//この関数の２回目以降のコール
        /* 次ホップリスト探索&送信 */
        nexthopl_prev = &Next_Hop_Addr_Info_Head[sPInfo.packetType];
        nexthopl_listnum = 0;
          
        for ( nexthopl_listnum = 0, nexthopl_cur = Next_Hop_Addr_Info_Head[sPInfo.packetType].next;
              nexthopl_cur != NULL;
              nexthopl_listnum++, nexthopl_cur = nexthopl_cur->next ) {
            printf("[sendPacketToNext %#x] unicast to ipDest %s\n"
                   ,pthread_self()
                   ,inet_ntoa(nexthopl_cur->ipDest));
                   
            UnicastPacket(sPInfo, nexthopl_cur);

            nexthopl_prev =  nexthopl_cur;

        }
            
    }
    //}
}

/*
/*  パケットを中継する
 *  @param struct sendingPacketInfo
 *  u_short        nextNodeType;     次ホップが子ノード:CHILDLEN, 親ノード:PARENT   
 *  u_short        packetType;       送信パケットタイプ(下部enum内の中から指定) 
 *  struct in_addr ipAddr;           CHILEDLEN->(親ノードのIPv4アドレスを指定 親がいなければ割り振られていないIPアドレス入れる) PARENT->(画像収集ノードのIP) 
 *  u_short        destPort;         パケット送信の宛先ポート                
 *  u_short        dataReceivePort; （中継時に指定）パケット受信用ポート     
 *  u_short        ackPortMin;       ACK受信用ポート最小値                  
 *  u_short        ackPortMax;       ACK受信用ポート最大値                  
 *  char          *packet;           送信するパケット   (指定なし)                       
 *  u_short        packetSize;       送信パケットサイズ (指定なし)
 */
void *relayPacket (void *param) {
    
    struct sendingPacketInfo  *sPInfo; 
    /*データ受信用ソケット*/

    int                       srcSocket;
    struct sockaddr_in        srcAddr;
    struct sockaddr_in        clientAddr;
    int                       clientAddrLen;
    u_int                     srcAddrLen;          // クライアントアドレス構造体の長さ
    int                       status;
    int                       numrcv;
    char                     *buffer;
    char                     *packet;             //シーケンス番号を除いてUnicastPacketの引数に指定
    u_short                   buffer_size;
    u_short                   packet_size;         //転送パケットサイズ(シーケンス番号を除く)
    char                     messACK [ACK_SIZE]; //パケット中の先頭2bytesをコピーして、SeindingACK
    u_short                  seqACK; //上記デバッグ用
    struct in_addr           destIP_ACK;
    u_short                  destPort_ACK;
    u_short                  myPort;
    int                      isDepPack=0; //==0なら受信・中継
    //ソケット再利用可能オプション
    int                      True = 1;
    //NACKによる再送か
    u_short                  isResend;

    //TCPのパケット受け付け用スレッドのID（特に使わない）
    pthread_t  threadID_recvTCPPacket;
    struct socketManagerForRelay *sockMang_new;
    //リレーパケットのデバッグ用
    struct timeval current_time;
    int            day,usec;
    double time_before_lock, time_after_lock;
    char *logfile = "logfile_relayPacket.log";
    FILE  *fp_relay;
    struct in_addr    sIP;
    u_short           from,to;
    u_char            low_delay;
    char              sysctl_command[100];
    
    sPInfo         = (struct sendingPacketInfo *)param;
    clientAddrLen  = sizeof(clientAddr);
   
    //受信用アドレス構造体の初期化
    memset(&srcAddr, 0, sizeof(srcAddr));
    myPort = sPInfo->dataReceivePort;
    
    //printf("[relayPacket %#x] ready to bind port %d\n",pthread_self(), myPort);

    srcAddr.sin_port        = htons(myPort); //中継用ポート
    srcAddr.sin_family      = AF_INET;
    srcAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    srcAddrLen = sizeof(srcAddr);


    if (sPInfo->protocolType == UDP) {
        //受信用バッファ領域の確保(受信しうるパケットサイズの最大値を指定=画像データパケット BMPはJPGよりも54bytes大きい)
        if(JPG_HANDLER) {
            packet_size = JPG_APP_PACKET_SIZE;
            buffer_size = NACKBASED_ARQ_HEADER_SIZE + JPG_APP_PACKET_SIZE;
        } else if (BMP_HANDLER) {
            packet_size = BMP_APP_PACKET_SIZE;
            buffer_size = NACKBASED_ARQ_HEADER_SIZE + BMP_APP_PACKET_SIZE;
        }
        //malloc
        if ((buffer =(char *)malloc(sizeof(char )*buffer_size)) == NULL) {
            err(EXIT_FAILURE,"fail to allocate memory (relayPacket)\n");
        }
        if((packet = (char *)malloc(sizeof(char )*packet_size)) == NULL) {
            err(EXIT_FAILURE,"fail to allocate memory (relayPacket)\n");
        }
        /* socket作る */
        if ((srcSocket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { /* UDPソケット */
            err(EXIT_FAILURE, "Socket cannot create exit.(relayPacket)\n");    
        }

        /*  再実行時にソケットがクローズ完了しない場合を考慮してソケットの再利用
         *  (他のアプリケーションとかぶらないようにする) */
        setsockopt(srcSocket, SOL_SOCKET, SO_REUSEADDR, &True, sizeof(int)); 

        if ((status = bind(srcSocket, (struct sockaddr *) &srcAddr, sizeof(srcAddr))) < 0) {
            err (EXIT_FAILURE, "Bind failed exit. (relayPacket) udpport %d\n",myPort);
        }
        //受信用ソケットバッファサイズ拡大(キューあふれがおこればすべてロスするので意味がない)
        /* sprintf   (sysctl_command, "sudo sysctl -w net.core.rmem_max=%d\n",SOCKET_BUFFER_SIZE_IMAGE); */
        /* system    (sysctl_command); */
        /* sprintf   (sysctl_command, "sudo sysctl -w net.core.rmem_default=%d\n",SOCKET_BUFFER_SIZE_IMAGE); */
        /* system    (sysctl_command); */
        /* printf("[relayPacket %#x] successfuly bind. Ready to relay packet at port %d\n" */
        /*        ,pthread_self() */
        /*        ,myPort); */
    
        for (;;) {
            //受信パケットサイズ取得(ソケット内の受信キュー内からデータ削除しない)
            numrcv     = recvfrom(srcSocket, buffer, buffer_size, MSG_PEEK,
                                  (struct sockaddr *) &clientAddr, &clientAddrLen);
            //printf("recv imgDataValMess size %d\n",numrcv);
            //データ取得(キューから削除)
            numrcv     = recvfrom(srcSocket, buffer, numrcv, 0,
                                  (struct sockaddr *) &clientAddr, &clientAddrLen);

            /* ACK 送信先IPアドレス・ポート ACk内容の決定*/
            destIP_ACK   = clientAddr.sin_addr;
            /* 受信時刻 */
            gettimeofday(&current_time, NULL);
            day   = (int)current_time.tv_sec;
            usec  = (int)current_time.tv_usec;
            /* NACK利用で中継するパケット */
            if(IMAGE_DATA_TRANSFER_WITH_HOPBYHOP_NACK_ARQ && sPInfo->packetType == IMAGE_DATA_PACKET) {
                packet_size = numrcv - IMAGE_DATA_TRANSFER_WITH_HOPBYHOP_NACK_ARQ;
                memcpy(&destPort_ACK, buffer,                   sizeof(u_short)); //ACK_port
                memcpy(messACK,       buffer+sizeof(u_short),   sizeof(u_short));//シーケンス番号
                memcpy(&isResend,     buffer+2*sizeof(u_short), sizeof(u_short));//シーケンス番号
                memcpy(packet,        buffer + IMAGE_DATA_TRANSFER_WITH_HOPBYHOP_NACK_ARQ, packet_size); //パケット本体
                /*NACK送信判断*/
                isDepPack=SendingACKorNACK (NACK, destIP_ACK, destPort_ACK, messACK, ACK_SIZE, isResend);


            } else {  /* ACK使用で中継するパケット */
                packet_size = numrcv - ARQ_HEADER_SIZE;
                memcpy(&destPort_ACK, buffer, sizeof(u_short)) ;         //ACK_port
                memcpy(messACK, buffer+sizeof(u_short), sizeof(u_short));//シーケンス番号
                memcpy(packet, buffer + ARQ_HEADER_SIZE, packet_size);   //パケット本体
                /*ACK送信*/
                isDepPack = SendingACKorNACK (ACK, destIP_ACK, destPort_ACK, messACK, ACK_SIZE, FIRST_PAC);
               
                //デバッグ
                memcpy(&seqACK, messACK, ACK_SIZE);
                /* ソースIP */
                memcpy(&sIP,    buffer+ ARQ_HEADER_SIZE, sizeof(struct in_addr));


                //期待シーケンス以外は中継処理しない
                if(isDepPack==0) {
                    /* sPInfoの初期化、パケット中継 */
                    /*     この他の項目はスレッドのcreate前に済ませている */
                   
                    //デバッグ
                    if((fp_relay = fopen(logfile, "a")) == NULL) {
                        err(EXIT_FAILURE, "fail to open file %s\n", logfile);
                    }
                    fprintf(fp_relay, "[relayPacket %#x %d.%06d] seq %d relayed to parent(source %s)\n"
                            ,pthread_self()
                            ,day
                            ,usec
                            ,seqACK
                            ,inet_ntoa(sIP));
                    fclose(fp_relay);
                    //デバッグここまで
                    sPInfo->packet     = packet;
                    sPInfo->packetSize = packet_size;
                    sPInfo->windowSize = WINDOW_SIZE;
                    sendPacketToNext(*sPInfo);
                } else {
                     //デバッグ
                    if((fp_relay = fopen(logfile, "a")) == NULL) {
                        err(EXIT_FAILURE, "fail to open file %s\n", logfile);
                    }
                    fprintf(fp_relay,
                            "[relayPacket %#x %d.%06d] seq %d is not relayed to parent (source %s)"
                            "(due to [duplicate] or [loss previous seq]\n"
                            ,pthread_self()
                            ,day
                            ,usec
                            ,seqACK
                            ,inet_ntoa(sIP)
                    );
                    fclose(fp_relay);
                    //デバッグここまで
                }
            }
        } //for loop

        
    } else if (sPInfo->protocolType == TCP ) {
        /* 接続の受付け */
        /* socket作る */
        if ((srcSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) { /* TCPソケット */
            err(EXIT_FAILURE, "Socket cannot create exit.(relayPacket)\n");    
        }
        /* ACK送信時にの時に有効になるか確かめる */
        /* low_delay = 46 << 2; */
        /*  setsockopt(srcSocket, SOL_IP ,IP_TOS,  &low_delay, sizeof(low_delay)); */

        /*  再実行時にソケットがクローズ完了しない場合を考慮してソケットの再利用
         *  (他のアプリケーションとかぶらないようにする) */
        setsockopt(srcSocket, SOL_SOCKET, SO_REUSEADDR, &True, sizeof(int)); 
        
        
        if ((status = bind(srcSocket, (struct sockaddr *) &srcAddr, sizeof(srcAddr))) < 0) {
            err (EXIT_FAILURE, "Bind failed exit. (relayPacket) tcpport %d\n",myPort);
        }
        printf("Waiting for connection at port ( ...\n");

        while(1) {
            /*新たなコネクションはヒープにソケットのための領域を取り、受信受付を行う*/
            sockMang_new = (struct socketManagerForRelay *) malloc(sizeof(struct socketManagerForRelay));
            //ヒープ領域のサイズ指定では間違いなので、型のサイズを指定
            memcpy (&sockMang_new->sPInfo , sPInfo, sizeof(struct sendingPacketInfo)); 

            /* 接続の許可 */
            if (listen(srcSocket, 1) != 0) {
                err(EXIT_FAILURE, "fail to listen\n");
            }
        
            if ((sockMang_new->socketInfo.clientSock
                 = accept(srcSocket, (struct sockaddr *) &clientAddr, &clientAddrLen)) < 0) {
                err(EXIT_FAILURE, "fail to accept\n");
            }
            /* 優先度設定 */
            low_delay = 46 << 2; 
            setsockopt(sockMang_new->socketInfo.clientSock, SOL_IP ,IP_TOS,  &low_delay, sizeof(low_delay)); 
            
                            
            //型が同じなら=でも構造体コピーになる
            sockMang_new->socketInfo.clientAddr = clientAddr;

            if ( pthread_create (&threadID_recvTCPPacket, NULL, recvTCPPacket, sockMang_new) < 0) {
                err(EXIT_FAILURE, "error occur\n");
            }
            
            printf("Connected from %s create thread %#x(give %p)\n"
                   ,inet_ntoa(clientAddr.sin_addr)
                   ,threadID_recvTCPPacket
                   ,sockMang_new);
            
        }
        
    }
    //ソケットクローズ(ここには来ない)
    close(srcSocket);

}

//TCPパケットを受信して、次のノードへリレーする
static void *recvTCPPacket (void *param){
    u_short  buffer_size; //考えられる受信パケットの最大サイズ
    u_short  numrcv;      //実際に受信したパケットサイズ
    char    *buffer;      //受信用バッファ

    
    struct socketManagerForRelay       *sockMang = (struct socketManagerForRelay *)param;;
    struct sendingPacketInfo   sPInfo = sockMang->sPInfo ; 
    struct sockaddr_in         clientAddr;
    int    clientAddrLen = sizeof(clientAddr);

    //受信用バッファ領域の確保(受信しうるパケットサイズの最大値を指定=画像データパケット BMPはJPGよりも54bytes大きい)
    if(JPG_HANDLER) {
        buffer_size = JPG_APP_PACKET_SIZE;
    } else if (BMP_HANDLER) {
        buffer_size = BMP_APP_PACKET_SIZE;
    }
    //malloc
    if ((buffer =(char *)malloc(sizeof(char )*buffer_size)) == NULL) {
        err(EXIT_FAILURE,"fail to allocate memory (relayPacket)\n");
    }
    
    while(1) {
     

        /* パケットサイズを取得してからキュー内のパケットを取得 */
        /* MSG_PEEKはキュー内にパケットを残すオプション */
        numrcv     = recv(sockMang->socketInfo.clientSock, buffer, buffer_size, MSG_PEEK);
        numrcv     = recv(sockMang->socketInfo.clientSock, buffer, numrcv, 0);

       
        if (numrcv <= 0) {//コネクションロスの場合は-1返すので、ソケットクローズして領域解放
            close(sockMang->socketInfo.clientSock);
            free(sockMang);
            return;

        } else {
                    
            printf("relayPacket  (from  %s clientPort %d) (status %d) (type %d)\n"
                   ,inet_ntoa(sockMang->socketInfo.clientAddr.sin_addr)
                   ,sockMang->socketInfo.clientAddr.sin_port
                   ,numrcv
                   ,sockMang->sPInfo.packetType);
            /*sPInfoの初期化、パケット中継*/
            /*この他の項目はスレッドのcreate前に済ませている*/
            sPInfo.packet     = buffer;
            sPInfo.packetSize = numrcv;
            /*次のノードへ送信*/
            sendPacketToNext(sPInfo);
            
        }
    }

  
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




/*[ここから先は工事中]*/
/* ACKを送信する (typeACK==ACKのとき)
 * NACKを送信する(typeACK==NACKのとき)
 * @param typeACK  ACKの動作タイプ
 * @param destIP   宛先IPアドレス    
 * @param srcPort  送信元ポートポート
 * @param destPort 宛先ポート
 * @param message  ACKの内容
 * @param messSize 上記のサイズ(Bytes)
 * @param isResend 受信パケットがNACKによって再送されたか否かを表す
 */
/* 返り値　重複シーケンス受信　-1 *
 *         連番                  0    
 *         シーケンスの飛びを確認 1
 */
int  SendingACKorNACK (u_short typeACK, struct in_addr destIP,
                       u_short destPort, char message[], u_short messSize, u_short isResend)
{
    struct sockaddr_in destAddr;      /* 宛先アドレス(送信用)      */
    int     destSock;
    u_short seqCur;   //今回受信シーケンス番号
    int     numsend;
    int     True = 1;
    u_short netord_destPort; //リスト内のポート番号をネットワークバイトオーダから直して格納

    //ACK用ソケットリスト探索用（cur:現在参照ポインタ、prev:１つ前, new:新規挿入用 proc:処理操作用
    struct ack_sock_item *ack_sock_cur, *ack_sock_prev, *ack_sock_new, *ack_sock_proc;
    /*NACK利用時に下記を使用 サフィックスの意味は上に準ずる*/
    struct tx_packet     *pac_cur, *pac_prev, *pac_next, *pac_new;
    struct sockaddr_in   destAddrNew;
    

    int    hit_ack_sock_list = 0; //ソケットリストヒット時1 ヒットなし0(当該IPアドレス、ポート番号への初ACK or NACK送信となる)
    char   tempaddr[20];
    int    listnum=0;
    int    val=1;                 //ノンブロッキング用

    int diffSeq;              //前回受信シーケンス番号と今回受信番号との差
    u_short numSeq;               //キュー挿入用
    u_short seqNACK;              //NACK内に記載する再送要求パケットシーケンス番号

    //シグナル タイマー
    int           signal_end_of_send = SIGNAL_SENDEND; //ACK受信に伴う送信スレッドの中断シグナル
    int           signal_enqueue     = SIGNAL_ENQUEUE;
    sigset_t      signal_set;

    //時刻記録用
    struct timeval current_time;
    int            day,usec;
    double time_before_lock, time_after_lock;

    u_char low_delay;
    //デバッグ用
    u_char  get_param;
    int     setsock_status;
    //ステータス用 (返り値)
    int     status = 0;

    FILE *fp_ack;
    char *logfile = "logfile_sendACK.log";
    
    //    u_short low_delay = 0x101;
    sigemptyset     ( &signal_set );
    sigaddset       ( &signal_set, signal_end_of_send );  // ブロック対象決定
    sigaddset       ( &signal_set, signal_enqueue);
    pthread_sigmask ( SIG_BLOCK, &signal_set, 0 );
    
    //シーケンス番号のコピー
    memcpy (&seqCur, message, messSize);
    
    /* printf("[ACK seq %d] to ip:%s destport %d about to be sent...\n" */
    /*        ,seqCur */
    /*        ,inet_ntoa(destIP) */
    /*        ,destPort); */

    /* 送信先IP、ポート番号に対応するACKソケットリスト探索 */
    ack_sock_prev     = &Ack_Sock_Item_Head;
    hit_ack_sock_list = 0;
    for ( ack_sock_cur = Ack_Sock_Item_Head.next;
          ack_sock_cur != NULL;
          ack_sock_cur = ack_sock_cur->next ) {
        
        /* printf("[SendingACK %#x] listnum %d serching ack sock list cur ip %s dest %s, curport %d destPort %d\n" */
        /*        ,pthread_self() */
        /*        , listnum */
        /*        , inet_ntoa(ack_sock_cur->destAddr.sin_addr) */
        /*        , inet_ntoa(destIP) */
        /*        , ntohs(ack_sock_cur->destAddr.sin_port) */
        /*        , destPort); */
        listnum++;
        //宛先IPアドレスとポート番号に対応するソケットを探索
        //アドレス構造体のポート番号はネットワークバイトオーダ(ビッグエンディアン)なので変換する必要がある
        if (!(memcmp (&destIP ,&ack_sock_cur->destAddr.sin_addr, sizeof(struct in_addr)))
           && destPort == ntohs(ack_sock_cur->destAddr.sin_port)) {
         
            hit_ack_sock_list = 1;
            ack_sock_proc     = ack_sock_cur;
            /* printf("[SendingACK %#x] socket is found.for ip %s, port %d\n" */
            /*        ,pthread_self() */
            /*        ,inet_ntoa( ack_sock_proc->destAddr.sin_addr) */
            /*        ,ntohs(ack_sock_proc->destAddr.sin_port)); */
            break;
        }
        ack_sock_prev =  ack_sock_cur;
    }
    
    if (hit_ack_sock_list == 0) { //ヒットなし->ソケットの項目新規作成
        ack_sock_new = (struct ack_sock_item *)malloc(sizeof(struct ack_sock_item));
        memset(&destAddrNew, 0, sizeof(destAddrNew));
        destAddrNew.sin_family       = AF_INET;
        destAddrNew.sin_addr         = destIP;
        destAddrNew.sin_port         = htons(destPort);
        memcpy (&ack_sock_new->destAddr, &destAddrNew, sizeof(destAddrNew));
        
        
        if ((ack_sock_new->destSock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            err(EXIT_FAILURE,"[SendingACK %#x] fail to create socket for ACK\n", pthread_self());     /*UDP*/
        }

        /* /\*ソケットクローズしていない場合の再利用(bindしなくなったから不要？)*\/ */
        /* setsockopt(ack_sock_new->destSock, SOL_SOCKET, SO_REUSEADDR, &True, sizeof(int));        */  
        //IPヘッダのTOSに最高レベルの優先度を設定（wireshark等でTOSフィールドを確認）
        low_delay = 46 << 2;
        setsock_status = setsockopt(ack_sock_new->destSock, SOL_IP ,IP_TOS,  &low_delay, sizeof(low_delay));

        get_param = 0;
      
        //この関数でブロックするのはまずいので、non-blocking
        ioctl(ack_sock_new->destSock, FIONBIO, &val);
        ack_sock_new->next         = NULL;
        ack_sock_prev->next        = ack_sock_new;
        ack_sock_proc              = ack_sock_new;
        ack_sock_proc->lastSeq     = 0;//初回の受信パケットのシーケンスは0番から
        ack_sock_proc->expectedSeq = 0;

        //以降、NACK送信時にのみ初期化が必要な項目
        if(typeACK == NACK) {
            ack_sock_proc->queueHead.next    = &ack_sock_proc->queueHead;//キュー内循環リスト構造
            ack_sock_proc->queueHead.prev    = &ack_sock_proc->queueHead;
            ack_sock_proc->queueMutex        = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
            ack_sock_proc->windowSize        = MAX_NACK_SEND_AT_SAME_TIME;
            ack_sock_proc->numMiddleSend     = 0;
            ack_sock_proc->waitingEnqueueSig = FALSE;
            ack_sock_proc->waitingDequeueSig = FALSE;
            
            /*NACK送信用スレッドを起動*/
            
            if( pthread_create(&ack_sock_proc->threadID_sendNACK,
                               NULL, sendingNACK, ack_sock_proc) != 0) {
                err(EXIT_FAILURE, "fail to create sendingNACK thread.\n");
            }
            /*NACKキューから要素を削除するスレッドを起動*/
            if( pthread_create(&ack_sock_proc->threadID_delQueue,
                               NULL, delNACKfromQueue, ack_sock_proc) != 0) {
                err(EXIT_FAILURE, "fail to create delNACKQueue thread\n");
            }
        }
        /* printf("[SendingACK %#x] new socket is create .for ip %s, port %d\n" */
        /*        ,pthread_self() */
        /*        ,inet_ntoa( ack_sock_proc->destAddr.sin_addr) */
        /*        ,ntohs(ack_sock_proc->destAddr.sin_port)); */
    }
    
    if(typeACK == ACK) {  //通常のACK送信
        get_param = 0;
        //getsockopt(ack_sock_proc->destSock, SOL_IP ,IP_TOS,  &get_param, sizeof(get_param));
        /* getsockopt(ack_sock_proc->destSock, SOL_SOCKET ,SO_PRIORITY,  &get_param, sizeof(get_param));  */
                
        /* printf("[SendingACK %#x] before ack sendto iptos %#x set %#x\n"  */
        /*        ,pthread_self(), IPTOS_LOWDELAY, get_param);  */
        diffSeq = (int)(seqCur - ack_sock_proc->expectedSeq);
        printf("deffseq %d, (cur %d expected %d)\n",diffSeq,seqCur,ack_sock_proc->expectedSeq);


        /* オーバーフロー時(U_SHRTMAXから0に戻るタイミング)の処理 */
        /* 条件としては、この例外となる確率は低いが確率0ではない。
         * RTOの上限値(極度に大きい値を設定しない)と[X*window size]の[X]を大きくすることで0に近づけることはできる */
        /* 期待シーケンス番号がU_SHRTMAX付近で、受信パケットがオーバーフロー後の0番以降であるとき、ACKを返さない */
        if(diffSeq >= (USHRT_MAX-200*WINDOW_SIZE)) {
            diffSeq-=USHRT_MAX;
        } else if (diffSeq <=(-1*USHRT_MAX+200*WINDOW_SIZE)) { /* U_SHTMAXからオーバーフローして0に戻る場合、65535以下はまだACK転送が完了していない可能性がある */
            diffSeq+=USHRT_MAX;
        }
            
        if(diffSeq <= 0) {      /* 期待シーケンス以外のACKを返してはならない(Go back N ARQ)がACKロスによる再送はACKを返す */
            numsend = sendto(ack_sock_proc->destSock, message, messSize
                             , 0, (struct sockaddr *)&ack_sock_proc->destAddr, sizeof(ack_sock_proc->destAddr));
            //ACK log
            if((fp_ack=fopen(logfile,"a")) == NULL) {
                err(EXIT_FAILURE, "%s", logfile);
            }
            gettimeofday(&current_time, NULL);
            day   = (int)current_time.tv_sec;
            usec  = (int)current_time.tv_usec;
            fprintf (fp_ack, "%d.%06d,send ack to %s:%d seq %d stat %d (expect %d diff %d)\n"
                     ,day,usec
                     ,inet_ntoa(ack_sock_proc->destAddr.sin_addr)
                     ,ntohs(ack_sock_proc->destAddr.sin_port)
                     ,seqCur
                     ,numsend
                     ,ack_sock_proc->expectedSeq
                     ,diffSeq
            );
            
            fclose  (fp_ack);
            if(diffSeq==0) {
                ack_sock_proc->lastSeq = seqCur;
                ack_sock_proc->expectedSeq = seqCur+1;
               
            } 

        } else {
            //ACK log
            if((fp_ack=fopen(logfile,"a")) == NULL) {
                err(EXIT_FAILURE, "%s", logfile);
            }
            gettimeofday(&current_time, NULL);
            day   = (int)current_time.tv_sec;
            usec  = (int)current_time.tv_usec;
            fprintf (fp_ack, "%d.%06d,(fail to)send ack to %s:%d seq %d (expect %d diff %d)\n"
                     ,day,usec
                     ,inet_ntoa(ack_sock_proc->destAddr.sin_addr)
                     ,ntohs(ack_sock_proc->destAddr.sin_port)
                     ,seqCur
                     ,ack_sock_proc->expectedSeq
                     ,diffSeq
            );
            fclose  (fp_ack);
                        
        }
        status = diffSeq;

        return status;
        
    } else if (typeACK == NACK) {//NACK送信

        /*以下、修正の必要あり*/
        
        //最初の受信の場合はシーケンス抜けパケット数の計算しない 再送パケットでない場合はシーケンス抜けを計算
        if (hit_ack_sock_list == 1  && isResend == FIRST_PAC) {
            if( (diffSeq = seqCur - ack_sock_proc->lastSeq) < 0) {
                diffSeq += USHRT_MAX; //seqCurがオーバーフローして0に戻る場合
                
            }
            printf("[SendingACKorNACK %#x] diff of sequence last  %d\n",pthread_self(), diffSeq);
            //バースト抜けの検証のため（あとで消す）
            if(diffSeq > 100) {
                printf("[SendingACKorNACK %#x] too large  %d\n",pthread_self(), diffSeq);
                
            }
            /* //シーケンス抜けの検知 NACK送信 */
            /* if (diffSeq > 1) { */
                
            //NACKキューにエンキュー
            for(numSeq = 1; numSeq < diffSeq; numSeq++) {        //エンキュー
                seqNACK = ack_sock_proc->lastSeq  + numSeq; //再送要求シーケンスの番号
                //項目
                if( (pac_new = (struct tx_packet *)malloc(sizeof(struct tx_packet))) == NULL) {
                    err(EXIT_FAILURE, "fail to allocate memory for NACK item\n");
                }
                //パケット本体
                if( (pac_new->packet = (char *)malloc(sizeof(u_short))) == NULL) {
                    err(EXIT_FAILURE, "fail to allocate memory for NACK packet.\n");
                }
                printf("[SendingACKorNACK %#x] requiring seq %d\n",pthread_self(), seqNACK);

                memcpy(pac_new->packet, &seqNACK, sizeof(u_short));
                pac_new->packetSize     = sizeof(u_short);
                pac_new->sequenceNumber = seqNACK;
                pac_new->timerResend    = NACK_TIMEOUT;
                pac_new->maxResend      = NACK_RETRY_MAX; 
                pac_new->queueMutex     = &ack_sock_proc->queueMutex;
                pac_new->destAddr       = ack_sock_proc->destAddr;
                pac_new->destSock       = &ack_sock_proc->destSock;
                /*  */
                pac_new->isThreadExists_sender = THREAD_READY;
                /*キュー要素削除スレッド*/
                pac_new->threadID_delQueue          = &ack_sock_proc->threadID_delQueue;
                //                    pac_new->isThreadExists_delQueue    = ack_sock_proc->;

                /* enqueue */
                //再送ログ
                    
                gettimeofday(&current_time, NULL);
                day   = (int)current_time.tv_sec;
                usec  = (int)current_time.tv_usec;
                time_before_lock = day + (double)usec/1000000;
                //printf("[SendingACKorNACK %#x] %d.%06d waiting for enqueue NACK\n");
                pthread_mutex_lock(&ack_sock_proc->queueMutex);
                time_after_lock  = day+ (double)usec/1000000;    
                gettimeofday(&current_time, NULL);
                day   = (int)current_time.tv_sec;
                usec  = (int)current_time.tv_usec;
                //printf("[SendingACKorNACK %#x] it takes %lf sec for unlock\n",time_after_lock-time_before_lock);
                ack_sock_proc->queueHead.next->prev =  pac_new;
                pac_new->next                       =  ack_sock_proc->queueHead.next;
                pac_new->prev                       = &ack_sock_proc->queueHead;
                ack_sock_proc->queueHead.next       =  pac_new;
                pthread_mutex_unlock(&ack_sock_proc->queueMutex);
                /*NACK送信スレッドがwaitingEnqueueならシグナル*/
                if( ack_sock_proc->waitingEnqueueSig == TRUE) {
                    //送信に失敗するとステータス返すが、無視する
                    pthread_kill (ack_sock_proc->threadID_sendNACK, signal_enqueue); 
                }

            }
            //}
            //シーケンスの更新、保存
            ack_sock_proc->lastSeq = seqCur;
        } else if (isResend == RESEND_PAC) {//再送されたパケットを正常に受信した場合、送信用スレッドを削除して、キューからNACKを消す
            
            //先頭（ヘッドから逆の方向の末端）の可能性が高いので、先頭から探索
            pthread_mutex_lock(&ack_sock_proc->queueMutex);
            pac_next = &ack_sock_proc->queueHead;
            pac_cur  = ack_sock_proc->queueHead.prev;
            pthread_mutex_unlock(&ack_sock_proc->queueMutex);
            //対応するsequenceNumberを再送しているNACKスレッドを探索。終了シグナル
            while( pac_cur != &ack_sock_proc->queueHead ) { 

                if(   pac_cur->sequenceNumber        == seqCur
                   && pac_cur->isThreadExists_sender == THREAD_START) {
                    //送信に失敗するとステータス返すが、無視する
                    printf("[SendingACKorNACK %#x] received seq %d. interrupt\n",pthread_self(),seqCur);
                    pthread_kill(ack_sock_proc->threadID_sendNACK, signal_end_of_send );
                    break;
                }
                pthread_mutex_lock(&ack_sock_proc->queueMutex);
                pac_next = pac_cur;
                pac_cur  = pac_cur->prev;
                pthread_mutex_unlock(&ack_sock_proc->queueMutex);
                
                
                
            }
        }
       
    }
    
    //最後のsizeofが怖い（テストで確認済みだが）
    //    close(destSock);
    
}

/* NACK送信用スレッド */
/* ack_sock_itemのtx_packetキューから次々に送信処理を開始する*/
/* @param struct ack_sock_item * (１つのIPアドレス、ポート番号に対してのソケット構造体) */
   
static void *sendingNACK     (void *param) {
    
    /*通信用パラメータ*/
    struct sockaddr_in destAddr;      /* 宛先アドレス(送信用)                   */
    u_short            destPort;      /* 宛先ポート                              */
   
    /*送信キューprev(nextの一つ前のポインタ),cur(操作対象のポインタ)とする*/
    struct tx_packet           *q_prev, *q_cur;
    /*引数の型変換*/
    struct ack_sock_item       * ack_sock_proc  = (struct ack_sock_item *)param;

    /*キュー内が空の状態でキューに新たなパケットがエンキューされたときに受信するシグナルの登録*/
    sigset_t    signal_set;
    int         signal_enqueue     = SIGNAL_ENQUEUE;
    int         signal_del_queue   = SIGNAL_DEL_QUEUE;

    sigset_t    signal_set_sender;
    int         signal_send_one_packet = SIGNAL_SEND_ONE_PACKET;
    
    int         val = 1; //ノンブロッキング
    
    /*シグナルの登録(queue) とブロック設定*/
    sigemptyset     ( &signal_set );
    sigaddset       ( &signal_set, signal_enqueue );       // enqueue
    sigaddset       ( &signal_set, signal_del_queue );   // dequeue
    pthread_sigmask ( SIG_BLOCK, &signal_set, 0 );

    sigemptyset     ( &signal_set_sender );
    sigaddset       ( &signal_set_sender, signal_send_one_packet );       // 1つのパケっト送信に必要なシグナル
    pthread_sigmask ( SIG_BLOCK, &signal_set_sender, 0 );

    pthread_kill( pthread_self(), signal_send_one_packet); //1回目の送信のため
    
    //スレッド存在中のフラグ
    ack_sock_proc->isThreadsNACKExists = THREAD_START;
  
    /* キュー中のNACK送信処理 */
    while(1) {
        /*!-lock-!*/
        pthread_mutex_lock  (&ack_sock_proc->queueMutex);
        q_prev = &ack_sock_proc->queueHead; //キュー先頭から(prevからキュー先頭を示す)
        q_cur  =  ack_sock_proc->queueHead.prev;
        pthread_mutex_unlock(&ack_sock_proc->queueMutex);
        /*!-unlock-!*/

        while (q_cur != &ack_sock_proc->queueHead  ) {
            /*printf("[UnicastWithARQ %#x] seq %d, state %d\n"
                   ,pthread_self()
                   ,q_cur->sequenceNumber
                   ,q_cur->isThreadExists_sender
                   );*/
            if(q_cur->isThreadExists_sender==THREAD_READY) { //送信スレッド起動前について

                //同時送信上限ならシグナル待ち
                if(ack_sock_proc->numMiddleSend >= MAX_NACK_SEND_AT_SAME_TIME) {
                  
                    /* 同時送信の上限に達しているのでデキュー待ち*/
                    ack_sock_proc->waitingDequeueSig = TRUE;
                    sigwait(&signal_set, &signal_del_queue); 
                    ack_sock_proc->waitingDequeueSig = FALSE;
                  
                } 
                /*キュー要素送信処理スレッド作成*/
                printf("[sendingNACK %#x] sending NACK requiring seq %d\n"
                       ,pthread_self()
                       ,q_cur->sequenceNumber);
                q_cur->threadID_createSender = pthread_self();
                q_cur->isThreadExists_sender = THREAD_START;
                                    
                if(pthread_create(&q_cur->threadID_sender, NULL, sendWaitAndResend, q_cur) != 0) {
                    err(EXIT_FAILURE, "sendWaitAndResend thread fail to create\n");
                }
             
                /*!-lock-!*/
                pthread_mutex_lock  (&ack_sock_proc->queueMutex); 
                ack_sock_proc->numMiddleSend++;     //送信処理中スレッド数
                
                pthread_mutex_unlock(&ack_sock_proc->queueMutex);
                /*!-unlock-!*/
            }
            /*!-lock-!*/
            pthread_mutex_lock  (&ack_sock_proc->queueMutex);
            q_cur = q_cur->prev;
            pthread_mutex_unlock(&ack_sock_proc->queueMutex);
            /*!-unlock-!*/

        } //すべて送信処理が終わり、ヘッドを指す

        /*エンキュー待ち*/       
        ack_sock_proc->waitingEnqueueSig = TRUE;
        sigwait(&signal_set, &signal_enqueue); //キューが空になったら SIGNAL_ENQUEUEを待つ
        //printf("[UnicastWithARQ %#x] enq signal received. ready to create sender thread.\n",pthread_self());
        ack_sock_proc->waitingEnqueueSig = FALSE;
    }

}

/* NACKキュー要素削除スレッド( */
/* NACKのsendWaitandResendが終了すると、このスレッドでキュー要素削除*/
static void *delNACKfromQueue(void *param) {
     /*型変換用*/
    struct ack_sock_item *ack_sock_proc;
    /*送信キューnext(curの一つ後方(先頭方向：ヘッドとは逆)のポインタ),cur(操作対象のポインタ)とする*/
    struct tx_packet     *q_next, *q_cur;
    //キュー削除用シグナルを受信したら、キューから要素削除を開始する。
    //シーケンス番号に対応するキュー要素を探索・再送制御スレッド&キュー要素削除スレッドへのシグナル送信
    sigset_t  signal_set;
    int       signal_del_queue = SIGNAL_DEL_QUEUE;
    //debugparam
    int      num_threads;
    sigemptyset     ( &signal_set );
    sigaddset       ( &signal_set, signal_del_queue );   // dequeue
    pthread_sigmask ( SIG_BLOCK, &signal_set, 0 );

    /*型変換*/
    ack_sock_proc = (struct ack_sock_item *)param;

    while(1) {
        //キュー要素削除シグナル受信までwait
        sigwait(&signal_set, &signal_del_queue);
        /*        printf("[delPacketFromQueue ID %#x] Delete packet from queue signal received.\n"
                  ,pthread_self());*/
        /*!-lock-!*/
        pthread_mutex_lock(&ack_sock_proc->queueMutex); //アクセスのロック

        q_next = &ack_sock_proc->queueHead;
        for( q_cur = ack_sock_proc->queueHead.prev; q_cur != &ack_sock_proc->queueHead; ) { //キュー先頭から最後尾まで検索

            if(q_cur->isThreadExists_sender == THREAD_EXIT) { //スレッドの終了→項目削除開始
                printf("[delPacketFromQueue ID %#x] Delete seq %d\n"
                       ,pthread_self()
                       ,q_cur->sequenceNumber);
                
                free(q_cur->packet);    //まずパケットの領域を解放
                q_cur = q_cur->prev;    
                free(q_next->prev);     //領域を解放
                q_next->prev = q_cur;
                q_cur->next  = q_next;
                ack_sock_proc->queueSize--;      //キューサイズ更新
                ack_sock_proc->numMiddleSend--;  //送信処理中のスレッド１つ分がデクリメント
                
                            
            } else if (q_cur->isThreadExists_sender == THREAD_READY) {
                //未送信状態のパケットが発見(以降は未送信であるためチェック不要)
                break;
            } 
            q_next = q_cur; //elseするの忘れてたために変なことになっていた可能性がある
            q_cur  = q_cur->prev;

            
        }
        /*printf("[delPacketFromQueue ID %#x] Delete seq %d (now qsize %d, num middle of send %d\n"
               ,pthread_self()
               ,ack_sock_proc->queueSize
               ,ack_sock_proc->numMiddleSend
               );*/
        
        pthread_mutex_unlock(&ack_sock_proc->queueMutex);
        /*!-unlock-!*/
        if(ack_sock_proc->waitingDequeueSig == TRUE) {
            
            //削除終わりのため、送信処理スレッドへのシグナル送信
            if(pthread_kill(ack_sock_proc->threadID_sendNACK, signal_del_queue) != 0) { 
                err(EXIT_FAILURE, "fail to send signal ()\n");
            }
        }
        //デバッグ
        /*        printf("[delPacketFromQueue %#x] delete %d packets\n"
               ,pthread_self()
               ,num_threads);*/
    }
   
}


/* ２つの文字列比較を行い、str1 eq str2なら0、それ以外は1を返す */
/* 注意 len(str1)<len(str2)は想定していない */
/* str1:比較対象 */
/* str2:探索パターン */
/* len : str2の文字列長 */
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

    /* printf("origip %s, net %s aft %s\n", s_orig_ip, s_orig_net, s_aft_net); /\*  *\/ */
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
