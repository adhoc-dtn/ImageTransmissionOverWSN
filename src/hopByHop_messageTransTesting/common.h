/*Time-stamp: <Wed Jan 29 11:35:56 JST 2014>*/

/*
 *クライアント・サーバ
 *インクルードライブラリ・マクロ・型定義定義共通
 */

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



/*ビーコン・制御メッセージ宛先(ブロードキャストアドレス)*/
#define  BROADCAST_IP          "192.168.50.255"
/*ビーコンIP(ロストしないようにしている) 本実験では有線となる*/
#define  BEACON_IP             "192.168.30.255"
/*#define BEACON_IP          BROADCAST_IP */


/* 以下２つの数を大きく取る際は、以降のポート設定値が重ならないようにする*/
/* １ホップ隣接子ノード最大値 */
#define NEIGHBOR_CHILDREN_MAX                  (20)
/* すべての子ノード最大値 */
#define ALL_CHILDLEN_MAX                       (20)

/* ビーコン受信してから画像データを送るまでの待ち時間（固定値）「実際は固定値＋ランダム数」 */
#define FIXED_WAIT_TIME (30)

/*
 * ポート番号リスト(ルートノード（画像収集ノード）→各子ノード（画像転送ノード）
 * 向きのパケットについては
 * ACKポートのMAX - MIN = (「１ホップ隣接子ノード」の想定最大数)と定義
 * 一方、各子ノード→親ノード方向のパケットについては、
 * ACKポートのMAX - MIN = (「自ノードが持つすべての子ノード」の想定最大数)と定義
 * したがって、後者のMAX - MINの間隔は前者よりも大きめに取ること
 *
 * ポート番号については、(Well known port 0=1024はroot権限が必要・
 * bindできないことがあるので使用しない)
 */

/* 以下、システムで使用するポート番号                */
/* 各ACK port はUDP上の再送制御有効時のみ使用       */
/* TCP使用時、ならびにUDPのみである場合は使用しない  */

/* (親ノード方向)画像データ待受ポート ACKポート最小最大 */
#define IMAGE_DATA_PORT                        (50000)
/*ネットワークトポロジが変わらない場合、以下のみを設定*/
#define IMAGE_DATA_ACK_PORT_MY                 (50001)
/*メッシュネットの場合のようにgatewayが時変化で変わる場合、下記を使用*/
#define IMAGE_DATA_ACK_PORT_MIN                (50002)
#define IMAGE_DATA_ACK_PORT_MAX                ( IMAGE_DATA_ACK_PORT_MIN + ALL_CHILDLEN_MAX )

/*(親ノード方向)転送データ量通知メッセージ待受ポート*/
#define NOTIFY_DATA_VOLUME_PORT                (50030)
/* MINとMAXのポート番号の差＝すべての子ノードの数（≠隣接ノード）なので注意*/
#define NOTIFY_DATA_VOLUME_ACK_PORT_MY         (50031)
#define NOTIFY_DATA_VOLUME_ACK_PORT_MIN        (50032)
#define NOTIFY_DATA_VOLUME_ACK_PORT_MAX        (NOTIFY_DATA_VOLUME_ACK_PORT_MIN + ALL_CHILDLEN_MAX  )

/* (子ノード方向)(画像送信ノード)新周期通知の待受ポート
 * MINとMAXのポート番号の差＝1ホップ隣接子ノード*/
#define CONTROL_MESS_PORT                      (50060)
#define CONTROL_MESS_ACK_PORT_MIN              (50061)
#define CONTROL_MESS_ACK_PORT_MAX              (CONTROL_MESS_ACK_PORT_MIN + NEIGHBOR_CHILDREN_MAX)

/*(子ノード方向)(画像送信ノード)初期周期広告待受用ポート
 * MINとMAXのポート番号の差＝1ホップ隣接子ノード*/
#define NOTIFY_INITIAL_PERIOD_PORT             (50090)
#define NOTIFY_INITIAL_PERIOD_ACK_PORT_MIN     (50091)
#define NOTIFY_INITIAL_PERIOD_ACK_PORT_MAX     (NOTIFY_INITIAL_PERIOD_ACK_PORT_MIN + NEIGHBOR_CHILDREN_MAX)

/*(子ノード方向)(画像送信ノード)プログラム終了命令待ち受け用ポート
 * MINとMAXのポート番号の差＝1ホップ隣接子ノード*/
#define PROGRAM_EXIT_MESS_PORT                 (50110)
#define PROGRAM_EXIT_MESS_ACK_PORT_MIN         (50111)
#define PROGRAM_EXIT_MESS_ACK_PORT_MAX         (PROGRAM_EXIT_MESS_ACK_PORT_MIN + NEIGHBOR_CHILDREN_MAX)

/* BMPヘッダサイズ(windowsV3形式を想定)*/
#define BMP_HEADER_SIZE                        (54)
/* ACKサイズ */
#define  ACK_SIZE                              (sizeof(u_short))
/* 画像データサイズ（シーケンス番号、ヘッダなど含めない）[注意]
 * IPレイヤのMTU未満に設定すること.MTU以上だとフラグメントする */
#define MAXIMUM_SEGMENT_SIZE                  (1350)


/*転送データ量通知メッセージサイズ (送信元IPv4アドレス+シーケンス開始部、終了部、合計転送サイズ)*/
#define NOTIFY_DATA_VOLUME_SIZE   (sizeof(struct in_addr)+2*sizeof(u_short)+sizeof(u_int))

/*受信用パケットサイズ(BMP)*/
/* 送信元IPアドレス 画像NO、データシーケンス番号 BMPヘッダ、segment == */
#define BMP_APP_PACKET_SIZE       (sizeof(struct in_addr) + 2*sizeof(u_short) + BMP_HEADER_SIZE + MAXIMUM_SEGMENT_SIZE)
#define JPG_APP_PACKET_SIZE       (sizeof(struct in_addr) + 2*sizeof(u_short) + MAXIMUM_SEGMENT_SIZE)

/*ソケット送信バッファサイズ(バッファ溢れを防ぐために1画像サイズ以上に調整しておく)*/
/* 3 Mbytes */
#define SOCKET_BUFFER_SIZE_IMAGE  (3000000)


/*転送する画像データ*/
/* JPG_RECEIVER : クライアントがJPGを送信
 * BMP_RECEIVER : クライアントがBMPを送信(どちらかを1にすること)
 * ZERO_PADDING : 抜けたシーケンスの部分を0でパディングして保存 */

/*初期周期広告とプログラム終了コードはcontrol methodに関係なく再送制御すること*/
enum {
  /*画像転送周期設定方式 STATIC:固定周期設定方式 ADAPTIVE:可変周期設定方式*/
  STATIC_PERIOD                                   = 0,
  ADAPTIVE_PERIOD                                 = 1,
  /*制御メッセージ（初期周期・新周期広告メッセージ）転送方式*/
  CONTROL_METHOD_UNICAST_WITH_HOPBYHOP_TCP        = 1, /* hop by hop TCPコネクション利用（川上方式）*/
  CONTROL_METHOD_UNICAST_WITH_APPLV_ARQ           = 0, /* APPレベル再送制御*/
  CONTROL_METHOD_BROADCAST                        = 0, /* ブロードキャスト */
  /*転送データ量通知メッセージ転送方式*/
  NOTIFY_DATA_VOLUME_WITH_HOPBYHOP_TCP            = 1, /* hop by hop TCPコネクション利用*/
  NOTIFY_DATA_VOLUME_WITH_HOPBYHOP_ARQ            = 0, /* ホップバイホップ再送制御 */
  NOTIFY_DATA_VOLUME_WITH_STRAIGHT_UNICAST        = 0, /* 画像収集ノード向けにユニキャスト */
  /*画像データ転送方式*/
  IMAGE_DATA_TRANSFER_WITH_ENDTOEND_ARQ           = 0, /*エンドツーエンドですべての受信パケットについてACKを送信。タイムアウト後に再送*/
  IMAGE_DATA_TRANSFER_WITH_HOPBYHOP_ARQ           = 0, /*ホップ倍ホップですべての受信パケットについてACKを送信。タイムアウト後に再送*/
  IMAGE_DATA_TRANSFER_WITH_HOPBYHOP_NACK_ARQ      = 0, /*(未完性)selective ack*/
  IMAGE_DATA_TRANSFER_WITH_STRAIGHT_UNICAST       = 1, /*UDPでユニキャスト*/
  /*室内実験時、下記１にする(有線で初期周期広告＆終了コードが送信される)*/
  FOR_INDOOR_EXPERIMENT                           = 1,
  /* シングルホップエミュレータ使用実験 */
  SINGLE_USING_NETEM                              = 1,
  /*画像データフォーマット*/
  JPG_HANDLER                                     = 0,
  BMP_HANDLER                                     = 1,
  /*受信、送信画像を残すか*/
  SAVE_PICTURES                                   = 1,
  /*画像の保存方式*/
  /* ZERO_PADDING                                    = 0, */
  
};

/* [ACK timeout とwindowsizeの注意事項] */
/* タイムアウト時に、最大、(1パケットサイズ* window size * 8 / RTO) bits/secパケットが1リンクに送信されるので */
/* [1パケットサイズ* window size * 8 / RTO]　> [1リンクの帯域]の条件を満たすと、再送が混雑を引き起こす */
/* RTOはリトライに応じて2倍になるが、
 * それのみならず window sizeは画像収集ノードが受信成功率に応じて、動的設定するべき*/

/*以下、enum内、 ***WITH_HOPBYHOP_ARQでのみ有効となるパラメータ*/
/*アプリケーションでのACK利用における最大再送回数*/
/* (現在無制限なので意味がない) */
#define ACK_RETRY_MAX             (3)
/* アプリケーションでのACKタイムアウト (s)*/
#define ACK_TIMEOUT               (0.5)
/*APP level ARQが適用されるときのヘッダサイズ*/
/* 先頭２バイト, ACK送信先ポート番号、後半2バイト、シーケンス番号*/
#define ARQ_HEADER_SIZE           (2*sizeof(u_short))
/*NACKBASED_ARQ適用の場合*/
#define NACKBASED_ARQ_HEADER_SIZE (3*sizeof(u_short))

/* 一度に送信処理可能な同時送信パケット数 WindoSize */
/* 1: stop and wait ARQと等しい*/

#define WINDOW_SIZE               (10)
/*送信用キュー長の上限*/
/*BMP１画像約800フラグメントなので、2倍程度*/
/* このサイズXパケットサイズ(1500bytes程度)＝mallocでパケット用に確保されるメモリの最大値
 * 9000程度のサイズになるとパケット送信用スレッドが作成できなくなるので、注意*/
/* 1画像データ分（大きすぎると遅延が大きくなる。キューに入ったパケットは必ず転送完了するまで再送され続けるので） */
#define TX_QUEUE_LENGTH_MAX         (400)

/*送信パケット入りキュー削除時の探索数最大(先頭からDEL_PACKET_QUEUE_SEARCH_MAX分だけ探索して終了する*/
/*forの実行速度と、ACK待ち時間を考慮する*/
#define DEL_PACKET_QUEUE_SEARCH_MAX  (WINDOW_SIZE)
/* SendingACKに指定するACKのタイプ(値を変更しないこと) */
enum {
    ACK  = 0,
    NACK = 1,
    /*他にもタイプが必要なら追記していく。連番になるように設定*/
};


/* 以下、 IMAGE_DATA_TRANSFER_WITH_HOPBYHOP_HIGHSPEED_ARQでのみ有効となるパラメータ*/
/* TX_QUEUE_LENGTH_MAX/CACHE_MAX_THRESHが最大キャッシュサイズとなる                                           */
/* 送信処理されたパケットはキャッシュに入って再送待状態となる。キャッシュ中にパケットが存在するときに               */
/* NACKを受信すると、対応するシーケンス番号のパケットが再送される。もしもキャッシュサイズがこのパラメータを超えると、*/
/* 最後にキャッシュに入ったものから削除される。NACKで送信するパケットがキャッシュ中になければ、再送は無視される     */
#define CACHE_MAX_THRESH           (5)
/*1つのソケットに対して、NACKの同時送信スレッド数最大値(同時処理可能スレッド最大数を考慮して設定)*/
#define MAX_NACK_SEND_AT_SAME_TIME (3)




/* NACKの再送回数 */
#define NACK_RETRY_MAX              (0)
/* NACKタイムアウト */
#define NACK_TIMEOUT                (0.0)
/*
 * ユニキャスト時に使用するSendinPacketToParentスレッドの引数として使用
 * 初期周期広告中の画像収集ノードIPと、自ノードへのパケット受信用ポート番号を指定する
 */
struct sendingPacketInfo {
    u_short        nextNodeType;    /* 次ホップが子ノード:CHILDLEN, 親ノード:PARENT   */  
    u_short        packetType;      /* 送信パケットタイプ(下部enum内の中から指定)      */
    struct in_addr ipAddr;          /* CHILEDLEN(親ノードのIPv4アドレスを指定) PARENT(画像収集ノードのIP) */
    u_short        destPort;        /* パケット送信の宛先ポート                      */
    u_short        protocolType;    /* トランスポートプロトコルタイプ                */
    char          *packet;          /* 送信するパケット                             */
    u_short        packetSize;      /* 送信パケットサイズ                          */
    /*中継する場合は必要*/
    u_short        dataReceivePort; /* 中継用パケット受信ポート           */

    /*以下、UDPベースの再送をする場合に有効なパラメータ*/
    u_short        ackPortMin;      /* ACK受信用ポート最小値(UDP再送があるときに有効) */
    u_short        ackPortMax;      /* ACK受信用ポート最大値                        */
    u_short        windowSize;      /* ACK受信を待たずに送信可能なパケット数        */
};



/*nextNodeType一覧*/
enum {
    CHILDLEN=0,
    PARENT,                     /* 1 */
};
/*トランスポートプロトコル*/
enum {
    TCP=0,
    UDP,
};

/*packetType一覧*/
enum {
    IMAGE_DATA_PACKET=0,    /* 画像データ               */
    AMOUNT_DATA_SEND_MESS,  /* 転送データ量通知メッセージ*/
    INITIAL_PERIOD_MESS,    /* 初期周期通知メッセージ    */
    NEW_PERIOD_MESS,        /* 新周期通知メッセージ      */
    EXIT_CODE,              /* 終了通知                  */
    PACKET_TYPE_NUM,        /* (これはパケットタイプではない)パケットタイプの種類合計        */
};

/*通信用デバイス*/
#define WIRELESS_DEVICE              "wlan0"
/*#define WIRELESS_DEVICE              "eth1"*/

/*キュー内にパケットが挿入されたときに発生させるシグナル*/
#define SIGNAL_ENQUEUE               (SIGUSR1)
/* キュー要素削除時にキュー要素削除スレッドに送るシグナル */
#define SIGNAL_DEL_QUEUE             (SIGUSR2)
/*ACK受信に伴うパケット送信スレッド終了シグナル*/
/*リアルタイムシグナルSIGRTMIN,MAXの最初の３個はLinuxThreadsで使われているらしいが、+10なら大丈夫だと思う*/
#define SIGNAL_FAIL                  (SIGRTMIN+ 7)
#define SIGNAL_SUCCESS               (SIGRTMIN+ 8)
#define SIGNAL_RESEND                (SIGRTMIN+ 9)
#define SIGNAL_TIMEOUT               (SIGRTMIN+10)
#define SIGNAL_SENDEND               (SIGRTMIN+11)
/* port open signal*/
#define SIGNAL_PORT_OPEN             (SIGRTMIN+12)
/*受信データ画像作成に伴う 項目削除用シグナル*/
#define SIGNAL_DEL_ITEM_IMAGE        (SIGRTMIN+13)
/* NACKキュー削除シグナル  */
#define SIGNAL_DEL_NACK_QUEUE        (SIGRTMIN+14) 
/* 前回パケット送信の完了シグナル*/
#define SIGNAL_SEND_ONE_PACKET       (SIGRTMIN+15)


/*以下、pthread_createで初期化したpthread_t型のスレッドIDを、プロセス内のスレッドID(ps -efLで確認)に変換する*/
/* (struct pthread型ポインタ)hoge = (struct pthead *) &(pthread_t型変数)fuga とかで*/
/* hoge->tid, hoge->pidでスレッドIDとプロセスIDがわかる*/
   
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



/*TCPサーバ用　各クライアントのコネクション管理構造体*/
struct socketManager {
    int                clientSock;                  //送信元とのコネクション用ソケット
    struct sockaddr_in clientAddr;   //クライアントのアドレス情報
    //  struct socketManager *next;
};

/*中継が含まれる場合*/
struct socketManagerForRelay {
    struct socketManager        socketInfo;
    struct sendingPacketInfo    sPInfo;

};



//ヘッド
//struct socketManager sM_Head;


/*
 * スレッドが実行前                            (READY)
 * スレッドが終了していて存在しない             (EXIT)
 * スレッドが実行中である                      (START)
 * スレッドがタイムアウトした                  (TIMEOUT)
 * スレッドは作成されない(ヘッドに挿入)         (NON)
 */
enum {
    THREAD_READY   = 0,
    THREAD_EXIT    = 1,
    THREAD_START   = 2,
    THREAD_TIMEOUT = 3,
    THREAD_NON     = 4,
};

/*状態*/
enum {
    FALSE = 0,
    TRUE  = 1,
};

/* 受信データ格納用バッファのサイズ(送信側バッファサイズ+ヘッダ以上のサイズにする) */
#define SIZE_DATA_BUFFER      (1600)

/*NACKの適用時 受信パケットの5~6bytes目の数値*/
enum {
    FIRST_PAC = 0, /*NACKによるものではない*/
    RESEND_PAC = 1, /*NACKして再送されたパケット*/
};
