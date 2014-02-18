/*Time-stamp: <Wed Jan 29 11:36:35 JST 2014>*/
/* coding utf-8 */

/* [TODO]
 *     X1 execVLC内では、取得画像のピクセル数変更も可能である(--scene-width=XXX)
 *        値小:画像サイズは小さく、値大:大きくなる
 *        方式そのものとして、フレームレートのみを変更するのではなく、
 *        画像特徴量抽出（openCV, matlab, scilab, octave等、[matlab以外は無料]）
 *        で特徴量が抽出可能なピクセル数であればOKにし、ピクセル数を小さくしつつ
 *        フレームレートを大きく（より頻繁に送信）する方法も考えられる
 *        [画像認識の観点で物体認識が可能な程度であればよい、というふうに制御]するので
 *        特徴量(といってもいろいろある)でなくとも、オプティカルフローとか適当に差分
 *        取る方式でも、考えられるやつ全部やる
 */

/* clinetImageSender.c
 * --概要--
 *        (固定周期・可変周期設定方式に関わらず)
 *        画像収集ノードのchangeImageSendingPeriod.c(サーバ)からの初期周期広告を受信すると、
 *        画像データの転送を開始する。ビーコン受信後BEACON_PORTは閉じられ、同様のビーコンを
 *        ブロードキャストする。その後ビーコンの広告周期ごとにwebカメラ/画像保存ディレクトリ
 *        から画像取得が行われ・UDPによる画像転送を行う
 *        もしも、周期制御メッセージを受信すると、画像転送周期を変更し、転送を続ける
 *
 * 
 *+++ 説明 +++
 *(はじめて使う人はよく読んでください) 
 *
 * 画像データをMAXIMUM_SEGMENT_SIZE(マクロ定義)に分割して周期的に送信する。
 * (可変/固定周期設定はサーバ側が行うので、本クライアントプログラムには選択項目はない)
 * 画像送信枚数INTERVAL_SEND毎に転送データ量通知メッセージを送信する
 * 画像データは
 *   (i)  JPEG
 *   (ii) BMP(WindowsV3)
 *  の２パターンが選択可能
 *
 * 各転送パターンを選択したとき、１パケットのフィールドは以下のように設定される
 * (i) JPEG転送時 (#define JPG_HANDLER)の場合
 *  _________________________________________________________________________
 * |  画像データ番号    | パケット番号      | ペイロード(JPEG)                  |
 * |   (u_short 2byte) | (u_short 2byte)  |  (最大 MAXIMUM_SEGMENT_SIZE byte) |
 *  ---------------------------------------------------------------------------
 *
 * (ii) BMP転送時 (#define BMP_SENDER)の場合 BMPヘッダロス時に再生できない問題があるので、
 *     毎回の送信時にBMPヘッダを含める。[オーバヘッドになるが、仕方ない]
 * ____________________________________________________________________________________________________
 * |  画像データ番号    | パケット番号        |  BMP(WindowsV3)ヘッダ |   ペイロード(JPEG )              |
 * |   (u_short 2byte) | (u_short 2byte)    |    (54 byte )        | (最大 MAXIMUM_SEGMENT_SIZE  byte)|
 * ---------------------------------------------------------------------------------------------------
 *
 * 転送データ量通知メッセージのフィールドは
 *
 *  ____________________________________________________________________________________
 * |シーケンス番号(転送はじめ)|シーケンス番号(転送終わり)|          合計転送データ量        |
 * |           (2 bytes)    |        (2 bytes)        |            (4 bytes)           |
 * -------------------------------------------------------------------------------------
 *
 * 初期周期広告のフィールド
 *  _________________________________________________________
 * |       初期周期           | 画像収集ノードIPアドレス       |
 * |     (float 4 bytes)     | (struct in_addr 4 bytes)      |
 * ----------------------------------------------------------
 *
 * 周期制御メッセージは各フィールドは
 *  _____________________________________________
 * | シーケンス番号     |          新周期         |
 * | (u_short 2 bytes) |       (float 4 bytes)   |
 * ----------------------------------------------

 
 *+++使い方++++ 
 *  プログラム中 (sudo) sysctlコマンドを用いています。
 *  プログラム実行ユーザがsudoでパスワード要求されないように、あらかじめ設定してください
 *  (sudoの[パスワード要求なし]のための設定はvisudoで行います)
 
 *------------------------------------------------------------------------------------------------------------------
 *              compile          : $ gcc clientImageSender.c -lpthread -o clientImageSender
 *              execute          : $ ./clientImageSender
 *------------------------------------------------------------------------------------------------------------------

 *++[未テスト項目]++
 * 
 * + MAXIMUM_SEGMENT_SIZEは1400以外でテストしたことないので、変更時に不具合が見られる可能性
 * + カメラから映像取得し送信はテスト済み。あらかじめ用意したファイルを送信する部分は未テスト
 */

#include "common.h"
#include <netdb.h>
#include <dirent.h>

/*カメラを使用して画像を取得するか、用意したファイルを使用するか*/
enum {
    USING_CAMERA = 1,
    USING_FILES  = 0,
};

/*--パケットサイズ、ペイロードサイズ--*/
/* 初期周期広告パケットサイズ ipv4+float = 8 bytes*/
#define INITIAL_CONTROL_MESS_SIZE     (sizeof(struct in_addr)+sizeof(float))
/* 新周期通知メッセージサイズ sequence(u_short) +new period(float) == 6 bytes*/
#define NEW_SENDING_PERIOD_MESS_SIZE  (sizeof(u_short)+sizeof(float))

/* ACK内容 */
#define ACK_MESS (1)


/*画像転送回数[現在未設定]*/
#define NUM_SEND_PICTURES         (1500)
/*転送データ量通知メッセージ送信インターバル(画像の枚数)*/
#define INTERVAL_SEND             (5)


/*転送画像ファイル名(VLCスナップショットのデフォルトファイル名)*/
#define SNAP_SHOT_FILE_NAME       "snapshot"
#define IMAGE_FILE_NAME           "image"

/*画像データ読み込みサイズ*/
#define BUFFER_READING_IMAGE  MAXIMUM_SEGMENT_SIZE


/*画像ファイルの拡張子*/
#define BMP_IMAGE_FORMAT          "bmp"

#define JPG_IMAGE_FORMAT          "jpg"



/*初期周期広告ブロードキャスト */
void BroadcastBeacon (struct in_addr ip_image_data_gathering_node, float param);
/*初期周期広告受信待ち*/
struct in_addr waitingBeacon();
/*初期周期広告ACK返す*/
void *replyBeaconACK (void *pram);
/*新周期広告のブロードキャスト*/
void BroadcastControlMessage (u_short sequence, float param);
/*制御メッセージ受信時に画像送信周期を変化*/
void *recvIntervalControlMessage(void *thread_arg);
/* プログラム終了命令受信待ち */
void *waitExitCode(void *thread_arg);
/*scandirのフィルタ*/
int scanDirFilter(const struct dirent * dir);
/*転送データ量通知メッセージの送信*/
void SendValueOfData (struct in_addr ipAddr, u_short start, ushort end, u_int value_of_data);
/* TCPでのメッセージ受信、転送 */
void *recvNewIntvlMessTCP (void *param);


/* packetSenderモジュールの初期化 */
extern void initializePacketSender();
/*無線IF向けの送信・転送関数*/
extern void sendPacketToNext (struct sendingPacketInfo sPInfo);
extern void *relayPacket     (void *sPInfo);
/*指定インタフェースのIPアドレス取得*/
extern struct in_addr GetMyIpAddr(char* device_name);
/*ACK*/
extern int SendingACKorNACK (u_short typeACK, struct in_addr destIP, u_short destPort,
                              char message[], u_short messSize, u_short isResend);




/*静止画送信周期 (メッセージ受信時に変化) */
double  SendingInterval;

/*制御メッセージによる送信周期変化検知 (なし:0 あり:1)*/
u_int   PeriodChange;



/* ビーコンブロードキャスト */
void BroadcastBeacon (struct in_addr ip_image_data_gathering_node, float param) {
    
    int i,j,n,sw,fs;
    int sock;                         /* ソケットディスクリプタ */
    struct sockaddr_in servAddr;      /* サーバのアドレス */
    struct sockaddr_in fromAddr;      /* 送信元のアドレス */
    unsigned short servPort;          /* サーバのポート番号 */
    unsigned int fromSize;            /* recvfrom()のアドレスの入出力サイズ */
    char *servIP;                     /* サーバのIPアドレス */
    char buf[sizeof(struct in_addr)+sizeof(float)];  //送信パケット格納用(画像転送ノードIP,初期画像転送周期広告)
    unsigned short int one = 1;
    int True = 1;

    
    //ソケット初期化
    servIP   = BEACON_IP; 
    servPort = NOTIFY_INITIAL_PERIOD_PORT;
        
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
      err(EXIT_FAILURE,"socket() failed");

    memset(&servAddr, 0, sizeof(servAddr));   
    servAddr.sin_family = AF_INET;                
    servAddr.sin_addr.s_addr = inet_addr(servIP); 
    servAddr.sin_port = htons(servPort);      
    //ブロードキャストオプション
    setsockopt(sock,SOL_SOCKET, SO_BROADCAST, (char *)&True, sizeof(True));
    
    //送信パケット作成
    memset(buf, 0, sizeof(buf));                      
    memcpy(buf, &ip_image_data_gathering_node, sizeof(struct in_addr)); 
    memcpy(buf+sizeof(struct in_addr), &param, sizeof(float)); 
    //送信
    n = sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&servAddr, sizeof(servAddr));
    
    printf("(gathering node) %s, sending period %.3f to %s(Broadcast)\n"
	   ,inet_ntoa(ip_image_data_gathering_node)
	   ,param
	   ,BROADCAST_IP);
    close(sock);
}

/* 画像収集ノードによる初期画像転送周期広告受信       */
/* 周期広告を受信するためにrecvfromでブロッキングする */
struct in_addr waitingBeacon() {
    unsigned short     myPort  = NOTIFY_INITIAL_PERIOD_PORT; /* port for recv control message*/
    int                srcSocket;
    int                clientSocket;
    int                status;
    struct sockaddr_in clientAddr;
    unsigned int      clientAddrLen = sizeof(clientAddr);   
    struct sockaddr_in srcAddr;
    struct in_addr     ip_image_data_gathering_node;
    int                numrcv;
    //受信パケット格納用(ACKポート 画像転送ノードIP,初期画像転送周期広告)
    //    char               buffer[INITIAL_CONTROL_MESS_SIZE];
    char               *buffer;
    u_short            buffer_size;
    float              interval;
    struct in_addr     destACK;
    u_short            portACK;
    char               messACK[ACK_SIZE];
    /*ACK送信スレッド用*/
    pthread_t          replyAck_thread_id;     /*初期周期広告のACK返すスレッド*/
    int                replyAck_status;
    int                True = 1;
    /*ユニキャスト用パラメータ*/
    u_short                  protocol_sequence;   //先頭2バイトはARQ用シーケンス番号となる
    struct sendingPacketInfo sPInfo;
    u_char             low_delay;
    
    //パケット受信用バッファメモリの確保
    if( CONTROL_METHOD_BROADCAST || CONTROL_METHOD_UNICAST_WITH_HOPBYHOP_TCP) {
        //無線IFのブロードキャストor有線IFでのブロードキャストの場合orACKのやりとりなし
        buffer_size = INITIAL_CONTROL_MESS_SIZE;
    } else { //ユニキャスト時はさらにheaderが必要
        buffer_size = ARQ_HEADER_SIZE + INITIAL_CONTROL_MESS_SIZE;
    }
    
    if( ( buffer = (char *)malloc( sizeof(char) * buffer_size ) )==NULL) {
        err(EXIT_FAILURE, "cannot allocate memory for buffer (INITIAL_CONTROL_MESS_SIZE) waitingBeacon\n");
    }
        
    
    memset(&srcAddr, 0, sizeof(srcAddr));
    srcAddr.sin_port        = htons(myPort);
    srcAddr.sin_family      = AF_INET;
    srcAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (CONTROL_METHOD_UNICAST_WITH_HOPBYHOP_TCP) {
        if((srcSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
            err(EXIT_FAILURE, "[waitingBeacon]fail to crate socket clientImageSender waitingBeacon\n");
        }
        /*ソケットのバインド(ソケットクローズしていない可能性があるためREUSE)*/
        setsockopt(srcSocket, SOL_SOCKET, SO_REUSEADDR, &True, sizeof(int)); 
        if ((status = bind(srcSocket, (struct sockaddr *) &srcAddr, sizeof(srcAddr))) < 0) {
            err(EXIT_FAILURE, "[waitingBeacon]cannot bind for waitingBeacon\n");
        }
        /* 接続の許可 */
        if (listen(srcSocket, 1) != 0) {
            err(EXIT_FAILURE, "[waitingBeacon]fail to listen\n");
        }
        if ((clientSocket = accept(srcSocket, (struct sockaddr *) &clientAddr, &clientAddrLen)) < 0) {
            err(EXIT_FAILURE, "[waitingBeacon]fail to accept\n");
        }
        low_delay = 46 << 2;
        setsockopt(clientSocket, SOL_IP ,IP_TOS,  &low_delay, sizeof(low_delay));
        
        sPInfo.ipAddr       = clientAddr.sin_addr; //次ホップへの送信対象として、前ホップのアドレスを除く
        //TCPはクライアント側のソケットで受信するので注意。自分のソケットはlistenとacceptのみ使う
        numrcv = recvfrom(clientSocket, buffer, buffer_size, 0,
                          (struct sockaddr *) &clientAddr, &clientAddrLen);
        close(srcSocket);
        close(clientSocket);

        memcpy (&ip_image_data_gathering_node, buffer, sizeof(struct in_addr)); //画像収集ノードIP
        memcpy (&interval, buffer + sizeof(struct in_addr), sizeof(float));     //画像転送周期
        printf("[waitingBeacon]Received %d bytes, final dest ip %s \n"
               ,numrcv
               ,inet_ntoa(ip_image_data_gathering_node)
        );

        //リレー用
        sPInfo.protocolType = TCP;
        sPInfo.packet       = buffer;
        sPInfo.packetSize   = numrcv;


    } else if (CONTROL_METHOD_UNICAST_WITH_APPLV_ARQ){
        if((srcSocket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            err(EXIT_FAILURE, "[waitingBeacon]fail to crate socket clientImageSender waitingBeacon\n");
        }
        /*UDPソケットのバインド(REUSEされる側もREUSEしないとだめみたいなのでここでset)*/
        setsockopt(srcSocket, SOL_SOCKET, SO_REUSEADDR, &True, sizeof(int)); 
        if ((status = bind(srcSocket, (struct sockaddr *) &srcAddr, sizeof(srcAddr))) < 0) {
            err(EXIT_FAILURE, "[waitingBeacon]cannot bind for waitingBeacon\n");
        }
    
        clientAddrLen = sizeof(clientAddr);
        memset(&clientAddr, 0, sizeof(clientAddr));
        //wait for beacon
        printf("sizeof(buffer == %d\n",sizeof(buffer));
        numrcv = recvfrom(srcSocket, buffer, buffer_size, 0,
                          (struct sockaddr *) &clientAddr, &clientAddrLen);
        close(srcSocket);
    
        memcpy (&portACK, buffer,                                             sizeof(u_short));        //ACK port
        memcpy (&protocol_sequence, buffer+sizeof(u_short),                   sizeof(u_short));        //sequence number
        memcpy (&ip_image_data_gathering_node, buffer+ARQ_HEADER_SIZE,        sizeof(struct in_addr)); //画像収集ノードIP
        memcpy (&interval, buffer + ARQ_HEADER_SIZE + sizeof(struct in_addr), sizeof(float));          //画像転送周期
    
        destACK  = clientAddr.sin_addr;
       
        memcpy (messACK, &protocol_sequence, ACK_SIZE);
        SendingACKorNACK (ACK, destACK, portACK, messACK, ACK_SIZE, FIRST_PAC);  //ACK
        /*ACK送信先IPアドレス(親ノード)*/
        printf("[waitingBeacon]Received %d bytes,sending ACK to ip %s, destport %d \n"
               ,numrcv, inet_ntoa(destACK), portACK);

        /*初期周期広告のACKリプライを返すためのスレッド*/

        replyAck_status = pthread_create(&replyAck_thread_id,
                                         NULL,
                                         replyBeaconACK,
                                         NULL);
        if( replyAck_status != 0){
            fprintf(stderr,"pthread_create : %s is failed",strerror(replyAck_status));
            exit(EXIT_FAILURE);
        }
        sPInfo.ipAddr       = clientAddr.sin_addr; //前ホップのアドレスを除く
        sPInfo.protocolType = UDP;
        sPInfo.packet       = buffer+ARQ_HEADER_SIZE;
        sPInfo.packetSize   = numrcv-ARQ_HEADER_SIZE;
        
    }
    //転送周期設定
    SendingInterval = interval; 
    printf("[waitingBeacon] received beacon. %.3f second and relay to childlen\n",interval); 
    sPInfo.nextNodeType = CHILDLEN;
    sPInfo.packetType   = INITIAL_PERIOD_MESS;
    sPInfo.destPort     = NOTIFY_INITIAL_PERIOD_PORT;
    sPInfo.ackPortMin   = NOTIFY_INITIAL_PERIOD_ACK_PORT_MIN;
    sPInfo.ackPortMax   = NOTIFY_INITIAL_PERIOD_ACK_PORT_MAX;
    sPInfo.windowSize   = WINDOW_SIZE;
    sendPacketToNext(sPInfo);
    /*mallocしたらfreeする*/
    free(buffer);
    return ip_image_data_gathering_node;
}

/* 
 * 初期周期広告 受信後にACKが送信側に届かない場合に
 * 再送されてきたメッセージに対してACKを返すだけ(賢くないコードなので、直したい)
 */
void *replyBeaconACK (void *pram) {
    u_short myPort  = NOTIFY_INITIAL_PERIOD_PORT; /* port for recv control message*/
    int     srcSocket;   
    int     status;
    struct sockaddr_in clientAddr;
    unsigned int       clientAddrLen;   
    struct sockaddr_in srcAddr;
    struct in_addr     ip_image_data_gathering_node;
    int     numrcv;
    char    *buffer; //受信パケット格納用(画像転送ノードIP,初期画像転送周期広告)
    u_short buffer_size;
    float   interval;
    u_short portACK;
    char    messACK[sizeof(u_short)];

    struct in_addr destACK;  //ACK送信先
    int     True = 1;

    //パケット受信用バッファメモリの確保
    if( CONTROL_METHOD_BROADCAST || ACK_RETRY_MAX==0) {
        //無線IFのブロードキャストor有線IFでのブロードキャストの場合orACKのやりとりなし
        buffer_size = INITIAL_CONTROL_MESS_SIZE;
    } else { //ユニキャスト時はさらに2bytes必要
        buffer_size = ARQ_HEADER_SIZE + INITIAL_CONTROL_MESS_SIZE;
    }
    
    if( (buffer = (char *)malloc(sizeof(char)*buffer_size)) == NULL ) {
        err(EXIT_FAILURE, "fail to allocate memory for buffer in replyBeaconAck\n");
    }
  
    /* 受信用ソケット作成  */
    memset(&srcAddr, 0, sizeof(srcAddr));
    srcAddr.sin_port        = htons(myPort);
    srcAddr.sin_family      = AF_INET;
    srcAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    srcSocket = socket(AF_INET, SOCK_DGRAM, 0);
    /*呼び出し元ソケットクローズ後だが、数分間はソケットが開放されないため再利用*/
    setsockopt(srcSocket, SOL_SOCKET, SO_REUSEADDR, &True, sizeof(int)); 
    if((status = bind(srcSocket, (struct sockaddr *) &srcAddr, sizeof(srcAddr)))<0) {
        err(EXIT_FAILURE,"erro occur while bind replyBeaconACK\n");
    }
    clientAddrLen = sizeof(clientAddr);


    while(1) {
        numrcv = recvfrom(srcSocket, buffer, buffer_size, 0,
                          (struct sockaddr *) &clientAddr, &clientAddrLen);
        memcpy (&portACK, buffer, sizeof(u_short));   //ACKポート
        memcpy (&messACK, buffer+sizeof(u_short), sizeof(u_short));  //sequence number
        destACK = clientAddr.sin_addr;
        SendingACKorNACK ( ACK, destACK, portACK, messACK, sizeof(u_short), FIRST_PAC ); /*ACK送信*/
    }
    //ここにはこない
    close(srcSocket);
    free(buffer);
  
    return NULL;

}

/* 有線IF向けに新周期通知メッセージをブロードキャストする */
void BroadcastControlMessage (u_short sequence, float param) {
    
    int i,j,n,sw,fs;
    int sock;                         /* ソケットディスクリプタ */
    struct sockaddr_in servAddr;      /* サーバのアドレス */
    struct sockaddr_in fromAddr;      /* 送信元のアドレス */
    unsigned short servPort;          /* サーバのポート番号 */
    unsigned int fromSize;            /* recvfrom()のアドレスの入出力サイズ */
    char *servIP;                     /* サーバのIPアドレス */
    char buf[sizeof(u_short)+sizeof(float)]; /* u_short 2 byte + 2 byte + float 4bytes*/
    
    unsigned short int one = 1;
    u_short field1;
    float   field2;
    int True = 1;

    field1   = sequence;
    field2   = param;
    servIP   = BROADCAST_IP;
    servPort = CONTROL_MESS_PORT; /* 指定のポート番号があれば使用 */
        
    /* UDPデータグラムソケットの作成 */
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
      err(EXIT_FAILURE,"socket() failed");
    
    /* サーバのアドレス構造体の作成 */
    memset(&servAddr, 0, sizeof(servAddr));   
    servAddr.sin_family = AF_INET;                
    servAddr.sin_addr.s_addr = inet_addr(servIP); 
    servAddr.sin_port = htons(servPort);      
   
    memset(buf, 0, sizeof(buf));                      
    memcpy(buf, &field1, sizeof(u_short)); 
    memcpy(buf+sizeof(u_short), &field2, sizeof(float)); 
    
    setsockopt(sock,SOL_SOCKET, SO_BROADCAST, (char *)&True, sizeof(True));
    n = sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&servAddr, sizeof(servAddr));
    
    printf("[Broadcast Control Message]\n"
	   "  sequence     =  %d\n"
	   "  new interval = %.3f to %s\n"
	   ,field1
	   ,field2
	   ,BROADCAST_IP);
    close(sock);
}

//TCP利用の場合、以下グローバルの必要あり(本来ロックも必要)
static u_short SequenceNumberVmessage=0;

/*制御メッセージ受信時に画像送信周期を変化させる(スレッド)*/
void *recvIntervalControlMessage(void *thread_arg) {
    unsigned short myPort  = CONTROL_MESS_PORT; /* port for recv control message*/
    int srcSocket;   
    int  status;
    char *clientIP;

    struct sockaddr_in clientAddr;
    unsigned int clientAddrLen= sizeof(clientAddr);   
    struct sockaddr_in srcAddr;
    int  numrcv;

    char  *buffer;
    FILE *periodChangeLog;
    char *logFileName = "logfile_recvCtrlMess.log";
    //制御メッセージシーケンスナンバー 初期シーケンスナンバーは1とする
    u_short field1;
    float   field2;
    //時刻記録用
    struct timeval current_time;
    int            day,usec;
    //ACK送信用パラメータ
    struct sendingPacketInfo sPInfo;
    int                      True = 1;
    struct in_addr           destACK;
    u_short                  portACK;
    char                     messACK[ACK_SIZE];
    u_short                  buffer_size;

    //TCP利用時に必要なパラメター
     //TCPのパケット受け付け用スレッドのID（特に使わない）
    pthread_t  threadID_recvNewIntvlMessTCP;
    struct socketManagerForRelay *sockMang_new;

    u_char low_delay;

    /* 重複or既受信パケット */
    int isDepPac = 0;
    
    if(CONTROL_METHOD_BROADCAST || CONTROL_METHOD_UNICAST_WITH_HOPBYHOP_TCP) { //無線IFのブロードキャストorTCP
        buffer_size = NEW_SENDING_PERIOD_MESS_SIZE;
        
    } else { //ユニキャスト + header size*必要
        buffer_size = ARQ_HEADER_SIZE + NEW_SENDING_PERIOD_MESS_SIZE;
    }
    //malloc
    if ((buffer =(char *)malloc(sizeof(char )*buffer_size)) == NULL) {
        err(EXIT_FAILURE,"fail to allocate memory (recvIntervalControlMessage)\n");
    }
    
    //受信用ソケット作成(UDP/TCP共通)
    srcAddr.sin_port        = htons(myPort);
    srcAddr.sin_family      = AF_INET;
    srcAddr.sin_addr.s_addr = htonl(INADDR_ANY);


   
    /* 初期画像転送周期の記録 以降では、周期更新があったときに記録*/
    if((periodChangeLog=fopen(logFileName,"a")) == NULL) {
        err(EXIT_FAILURE, "%s", logFileName);
    }
    gettimeofday(&current_time, NULL);
    day   = (int)current_time.tv_sec;
    usec  = (int)current_time.tv_usec;
    fprintf (periodChangeLog, "%d.%06d,initialperiod,%lf\n",day,usec,SendingInterval);
    fclose  (periodChangeLog);

    /* 以下、TCP/UDPによる受信処理 */
    if(CONTROL_METHOD_UNICAST_WITH_HOPBYHOP_TCP) {
        //次ホップへの送信用パラメータ初期化
        sPInfo.nextNodeType = CHILDLEN;
        sPInfo.packetType   = NEW_PERIOD_MESS;
        // sPInfo.ipAddr       = clientAddr.sin_addr; //宛先のうち画像収集ノードの方向のアドレスをフィルタ
        sPInfo.destPort     = CONTROL_MESS_PORT;
        sPInfo.protocolType = TCP;
        sPInfo.ackPortMin   = CONTROL_MESS_ACK_PORT_MIN;
        sPInfo.ackPortMax   = CONTROL_MESS_ACK_PORT_MAX;
        sPInfo.packetType   = NEW_PERIOD_MESS;
        sPInfo.ipAddr       = clientAddr.sin_addr; //画像収集ノードのアドレス

        /* 接続の受付け */
        /* socket作る */
        if ((srcSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) { /* TCPソケット */
            err(EXIT_FAILURE, "Socket cannot create exit.(relayPacket)\n");    
        }

        /*  再実行時にソケットがクローズ完了しない場合を考慮してソケットの再利用
         *  (他のアプリケーションとかぶらないようにする) */
        setsockopt(srcSocket, SOL_SOCKET, SO_REUSEADDR, &True, sizeof(int)); 
        low_delay = 46 << 2;
        setsockopt(srcSocket, SOL_IP ,IP_TOS,  &low_delay, sizeof(low_delay));
        
        if ((status = bind(srcSocket, (struct sockaddr *) &srcAddr, sizeof(srcAddr))) < 0) {
            err (EXIT_FAILURE, "Bind failed exit. (relayPacket) tcpport %d\n",myPort);
        }
        printf("Waiting for connection at port ( ...\n");

        while(1) {
            
            /* 受信用スレッド作る(スレッド内でsendPacketTonext) */
            /*新たなコネクションはヒープにソケットのための領域を取り、受信受付を行う*/
            sockMang_new = (struct socketManagerForRelay *) malloc(sizeof(struct socketManagerForRelay));
            
            
            /* 接続の許可 */
            if (listen(srcSocket, 1) != 0) {
                err(EXIT_FAILURE, "fail to listen\n");
            }
        
            if ((sockMang_new->socketInfo.clientSock
                 = accept(srcSocket, (struct sockaddr *) &clientAddr, &clientAddrLen)) < 0) {
                err(EXIT_FAILURE, "fail to accept\n");
            }

            low_delay = 46 << 2;
            setsockopt(sockMang_new->socketInfo.clientSock, SOL_IP ,IP_TOS,  &low_delay, sizeof(low_delay));

            sPInfo.ipAddr = clientAddr.sin_addr;//１ホップ前送信元ノード方向へのパケットを、次ホップにしない
            sockMang_new->socketInfo.clientAddr = clientAddr;
            sockMang_new->sPInfo                =  sPInfo;


            
            if ( pthread_create (&threadID_recvNewIntvlMessTCP
                                 ,NULL
                                 ,recvNewIntvlMessTCP
                                 ,sockMang_new) < 0) {
                err(EXIT_FAILURE, "error occur\n");
            }
            
            printf("Connected from %s create thread %#x(give %p)\n"
                   , inet_ntoa(clientAddr.sin_addr), threadID_recvNewIntvlMessTCP, sockMang_new);
        }
    
    } else {
        srcSocket = socket(AF_INET, SOCK_DGRAM, 0);

        //ソケットを再利用可能に
        setsockopt(srcSocket, SOL_SOCKET, SO_REUSEADDR, &True, sizeof(int)); 
        //バインド
        if ((status = bind(srcSocket, (struct sockaddr *) &srcAddr, sizeof(srcAddr))) < 0) {
            err (EXIT_FAILURE, "cannot bind for recvIntervalControlMessage\n");
        }


    
        while(1) {
       
            memset(&srcAddr, 0, sizeof(srcAddr));
            numrcv = recvfrom(srcSocket, buffer, buffer_size, 0,
                              (struct sockaddr *) &clientAddr, &clientAddrLen);
       
            if(CONTROL_METHOD_UNICAST_WITH_APPLV_ARQ) { //ACK
                memcpy (&portACK, buffer,                                sizeof(u_short));
                memcpy (messACK,  buffer+sizeof(u_short),                sizeof(u_short));
                destACK = clientAddr.sin_addr;
                isDepPac = SendingACKorNACK (ACK, destACK, portACK, messACK, ACK_SIZE, FIRST_PAC);
                memcpy (&field1, buffer+ARQ_HEADER_SIZE,                 sizeof(u_short));
                memcpy (&field2, buffer+ARQ_HEADER_SIZE+sizeof(u_short), sizeof(float));
            } else {
                memcpy (&field1, buffer,                 sizeof(u_short));
                memcpy (&field2, buffer+sizeof(u_short), sizeof(float));
            }

            if (isDepPac == 0) {
                clientIP = inet_ntoa(clientAddr.sin_addr);
                //メッセージが受信された
                printf("message received seq, %d new_per %f\n",field1,field2);


            
                if(field1 > SequenceNumberVmessage) { //すでに受信したシーケンス番号のデータは廃棄
                    SequenceNumberVmessage = field1;
	
                    SendingInterval = field2; //set new interval
                    PeriodChange    = 1;      //送信タイマーセットのため タイマーセット後0に戻る
        
                    printf("[Received message]-------------------------\n" 
                           " sequence        : %3d\n"
                           " from            : %s\n" 
                           " new interval    : %f\n" 
                           ,field1
                           ,clientIP 
                           ,SendingInterval);

                    if(CONTROL_METHOD_BROADCAST) {            //ブロードキャストorユニキャスト
                        BroadcastControlMessage(field1, field2);
                    } else { //sendPacketToNExtにもブロードキャストが実装されているがbufferの場合分けが必要
                        sPInfo.nextNodeType = CHILDLEN;
                        sPInfo.protocolType = UDP;
                        sPInfo.packetType   = NEW_PERIOD_MESS;
                        sPInfo.ipAddr       = clientAddr.sin_addr; //画像収集ノードのアドレス
                        sPInfo.destPort     = CONTROL_MESS_PORT;
                        //        sPInfo.dataReceivePort
                        sPInfo.ackPortMin   = CONTROL_MESS_ACK_PORT_MIN;
                        sPInfo.ackPortMax   = CONTROL_MESS_ACK_PORT_MAX;
                        sPInfo.packet       = buffer+ARQ_HEADER_SIZE;
                        sPInfo.packetSize   = numrcv-ARQ_HEADER_SIZE;
                        sPInfo.windowSize   = WINDOW_SIZE;
                        sendPacketToNext(sPInfo);
                    }
                    //新周期記録
                    if((periodChangeLog=fopen(logFileName,"a")) == NULL) {
                        err(EXIT_FAILURE, "%s", logFileName);
                    }
                    gettimeofday(&current_time, NULL);
                    day   = (int)current_time.tv_sec;
                    usec  = (int)current_time.tv_usec;
                    fprintf (periodChangeLog
                             ,"%d.%06d,newperiod,%lf,seq,%d\n"
                             ,day,usec,SendingInterval,field1);
                    fclose (periodChangeLog);
           
	
                }
            }
        }
    }
    close(srcSocket); //ソケット（クローズできないけど）
    return NULL;
}
/* param
 * @param struct socketManagerForRelay * 中継送信用のパラメータ
 */
void *recvNewIntvlMessTCP (void *param) {

    u_short  packet_size; //パケット本体のサイズ
    u_short  numrcv;      //実際に受信したパケットサイズ
    char    *packet;      //受信用バッファ
    //制御メッセージシーケンスナンバー 初期シーケンスナンバーは1とする
    u_short field1;
    float   field2;
    char    clientIP[20];
    struct sockaddr_in clientAddr;
    int clientAddrLen = sizeof(clientAddr);
    struct socketManagerForRelay   *sockMang = (struct socketManagerForRelay *)param;;
    struct sendingPacketInfo   sPInfo = sockMang->sPInfo ; 
    FILE *periodChangeLog;
    char *logFileName = "logfile_recvCtrlMess.log";
   //時刻記録用
    struct timeval current_time;
    int            day,usec;
    
    //受信用バッファ領域の確保(受信しうるパケットサイズの最大値を指定=画像データパケット BMPはJPGよりも54bytes大きい)
    if(JPG_HANDLER) {
        packet_size = JPG_APP_PACKET_SIZE;
        
    } else if (BMP_HANDLER) {
        packet_size = BMP_APP_PACKET_SIZE;
    }
    //malloc
    if ((packet =(char *)malloc(sizeof(char )*packet_size)) == NULL) {
        err(EXIT_FAILURE,"fail to allocate memory (relayPacket)\n");
    }
   
    while(1) {
        //受信パケットサイズ取得(ソケット内の受信キュー内からデータ削除しない)
        numrcv     = recvfrom(sockMang->socketInfo.clientSock, packet, packet_size, MSG_PEEK,
                              (struct sockaddr *) &clientAddr, &clientAddrLen);
      
        //データ取得(下位のキューから削除)
        numrcv     = recvfrom(sockMang->socketInfo.clientSock, packet, numrcv, 0,
                              (struct sockaddr *) &clientAddr, &clientAddrLen);
            
        if (numrcv <= 0) {//コネクションロスの場合は-1返すので、ソケットクローズして領域解放
            close(sockMang->socketInfo.clientSock);
            free(sockMang);
            return;
        }
        memcpy (&field1, packet,                 sizeof(u_short));
        memcpy (&field2, packet+sizeof(u_short), sizeof(float));
        
              
        if(field1 > SequenceNumberVmessage) { //すでに受信したシーケンス番号のデータは廃棄(この条件は適当だが)
            SequenceNumberVmessage = field1;
            /*この他の項目はスレッドのcreate前に済ませている*/
            
            sPInfo.packet     = packet;
            sPInfo.packetSize = packet_size;
            /*次のノードへ送信*/
            sendPacketToNext(sPInfo);

            memcpy(clientIP, inet_ntoa(sockMang->socketInfo.clientAddr.sin_addr), sizeof(clientIP));
                        
            SendingInterval = field2; //set new interval
            PeriodChange    = 1;      //送信タイマーセットのため タイマーセット後0に戻る
            printf("[Received message]-------------------------\n"
                   " sequence        : %3d\n"
                   " from            : %s\n"
                   " new interval    : %f\n"
                   ,field1
                   ,clientIP
                   ,SendingInterval);

             //新周期記録
            if((periodChangeLog=fopen(logFileName,"a")) == NULL) {
                err(EXIT_FAILURE, "%s", logFileName);
            }
            gettimeofday(&current_time, NULL);
            day   = (int)current_time.tv_sec;
            usec  = (int)current_time.tv_usec;
            fprintf (periodChangeLog, "%d.%06d,newperiod,%lf,seq,%d\n"
                     ,day,usec,SendingInterval
                     ,field1);
            fclose (periodChangeLog);
            
       
            
        }
    }

}
/*VLCによる画像取得用スレッド*/
void *execVLC(void *thread_arg)
{
    char command[100];
    char *imageFormat;
  
    if (BMP_HANDLER) {
        imageFormat   = BMP_IMAGE_FORMAT;
    } else if (JPG_HANDLER) {
        imageFormat = JPG_IMAGE_FORMAT;
    }
    //--scene-widthの調節でピクセル数=ファイルサイズ変更できる
    sprintf(command, 
            "~/vlc-2.0.5/vlc -I \"dummy\" \"$@\" -vvv v4l2:///dev/video0 --video-filter=scene --vout=dummy --no-audio --scene-format=%s --scene-ratio=10 --scene-width=500 --scene-prefix=%s --scene-replace  --scene-path=. \n",
            imageFormat,
            SNAP_SHOT_FILE_NAME);
    system(command);
    /*VLCは実行状態になるので以下は実行されない*/

}
 
/* プログラム終了命令受信待ち         */
/* ユニキャストで送ってくるようにする */
/* この部分はTCPの前提でよいだろう*/
void *waitExitCode(void *thread_arg)
{
    unsigned short     myPort  = PROGRAM_EXIT_MESS_PORT; /* port for recv control message*/
    int                srcSocket;
    int                clientSocket;
    int                status;
    struct sockaddr_in clientAddr;
    unsigned int      clientAddrLen = sizeof(clientAddr);   
    struct sockaddr_in srcAddr;
    int                numrcv;
    u_short            code_number = 0;       //プログラム終了コードを受信したら終了
    char               *buffer;
    u_short            buffer_size;
    //reuseaddr
    int                True = 1;
    struct in_addr     destACK;
    u_short            portACK;
    char               messACK[ACK_SIZE];
    struct sendingPacketInfo sPInfo;
    //struct in_addr     ipRoot = (struct in_addr)thread_arg;

    buffer_size = sizeof(u_short)+ARQ_HEADER_SIZE ;

    /* //パケットサイズ決定 */
    /* if(CONTROL_METHOD_BROADCAST || FOR_INDOOR_EXPERIMENT || CONTROL_METHOD_UNICAST_WITH_HOPBYHOP_TCP) { */
    /*     buffer_size = sizeof(u_short) ; */
    /* } */ /* else if (CONTROL_METHOD_UNICAST_WITH_APPLV_ARQ) { */
    /*     buffer_size = sizeof(u_short) + ARQ_HEADER_SIZE; */
    /* } */
    if((buffer = (char *)malloc(sizeof(u_short)))==NULL) {
        err(EXIT_FAILURE, "cannot allocate memory for buffer (INITIAL_CONTROL_MESS_SIZE) waitingBeacon\n");
    }
    //ポート開け、受信待ち状態
    memset(&srcAddr, 0, sizeof(srcAddr));
    srcAddr.sin_port        = htons(myPort);
    srcAddr.sin_family      = AF_INET;
    srcAddr.sin_addr.s_addr = htonl(INADDR_ANY);
      
    if((srcSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        err(EXIT_FAILURE, "fail to create socket clientImageSender waitngExitCode\n");
    }
    setsockopt(srcSocket, SOL_SOCKET, SO_REUSEADDR, &True, sizeof(int)); 

    if((status = bind(srcSocket, (struct sockaddr *) &srcAddr, sizeof(srcAddr))) < 0){
        err(EXIT_FAILURE, "fail to bind code for exit\n");
    }
    
    clientAddrLen = sizeof(clientAddr);

    while(1) {
       
        /* /\* 接続の許可 *\/ */
        /* if (listen(srcSocket, 1) != 0) { */
        /*     err(EXIT_FAILURE, "fail to listen\n"); */
        /* } */
        /* if ((clientSocket = accept(srcSocket, (struct sockaddr *) &clientAddr, &clientAddrLen)) < 0) { */
        /*     err(EXIT_FAILURE, "fail to accept\n"); */
        /* } */

        /* numrcv = recvfrom(clientSocket, buffer, buffer_size, 0, */
        /*                   (struct sockaddr *) &clientAddr, &clientAddrLen); */

        numrcv = recvfrom(srcSocket, buffer, buffer_size, 0, 
                          (struct sockaddr *) &clientAddr, &clientAddrLen);

        memcpy (&portACK, buffer, sizeof(u_short));   //ACKポート
        memcpy (&messACK, buffer+sizeof(u_short), sizeof(u_short));  //sequence number
        destACK = clientAddr.sin_addr;
        SendingACKorNACK ( ACK, destACK, portACK, messACK, sizeof(u_short), FIRST_PAC ); /*ACK送信*/
        memcpy (&code_number, buffer+ARQ_HEADER_SIZE, sizeof(u_short)); /*code number抜き出し  */

        //リレー用

        sPInfo.nextNodeType = CHILDLEN;
        sPInfo.packetType   = EXIT_CODE;
        sPInfo.protocolType = UDP;
        sPInfo.ipAddr       = clientAddr.sin_addr; //次ホップへの送信対象として、前ホップのアドレスを除く
        sPInfo.packet       = buffer;
        sPInfo.packetSize   = sizeof(u_short); /* パケット本体のサイズ */
        sPInfo.destPort     = PROGRAM_EXIT_MESS_PORT;
        sPInfo.ackPortMin   = PROGRAM_EXIT_MESS_ACK_PORT_MIN;
        sPInfo.ackPortMax   = PROGRAM_EXIT_MESS_ACK_PORT_MAX;
        sPInfo.windowSize   = WINDOW_SIZE;
        sendPacketToNext(sPInfo);
      
        /* if(code_number == 1) { //終了 */
            close(srcSocket);
            close(clientSocket);
            printf("[This program received exit code: number (%d)\n",code_number); 
            system ("killall vlc");
            sleep(20);
            exit (EXIT_SUCCESS);
        /* } */
    }
}

/* scandirのフィルタ */
int scanDirFilter(const struct dirent * dire){

    /* Discard . and .. */
    if( strncmp(dire->d_name, ".", 2) == 0
        || strncmp(dire->d_name, "..", 3) == 0 )
        return 0;

    /* Check whether it is a DIR or not.
    * Some FS doesn't handle d_type, so we check UNKNOWN as well */
    /*    if( dire->d_type != DT_UNKNOWN
            && dire->d_type != DT_DIR )
        return 0;
    */
   
    return 1;
}


/*転送データ量通知メッセージの送信*/
void SendValueOfData (struct in_addr ipAddr, u_short start, ushort end, u_int value_of_data) {
    
    int sock;                                        /* ソケットディスクリプタ                     */
    int sent_suc;                     
    struct sockaddr_in servAddr;                     /* サーバのアドレス                          */
    struct sockaddr_in fromAddr;                     /* 送信元のアドレス                          */
    
    unsigned short     servPort;                     /* サーバのポート番号                        */
    char               buf[NOTIFY_DATA_VOLUME_SIZE]; /* ipv4 addr 4 bytes + u_short 2 byte + 2 byte, unsigned 4 byte */
    struct in_addr     ipMy;                       /* ヘッダに含めるインタフェースIPアドレス     */
    u_short            field1 = 0;
    u_short            field2 = 0;
    u_int              field3 = 0;

    //送信ログファイル作成用
    FILE *fp_sendval;
    char *logfilename = "logfile_imageSender_sentValMess.log";
    struct timeval current_time;
    int            day,usec;
    //ユニキャスト用
    struct sendingPacketInfo sPInfo;

    ipMy         = GetMyIpAddr(WIRELESS_DEVICE);
    field1       = start;
    field2       = end;
    field3       = value_of_data;

     //パケット初期化
    memcpy(buf,                                         &ipMy,   sizeof(struct in_addr));
    memcpy(buf+sizeof(struct in_addr),                  &field1, sizeof(u_short));
    memcpy(buf+sizeof(struct in_addr)+sizeof(u_short),  &field2, sizeof(u_short));
    memcpy(buf+sizeof(struct in_addr)+2*sizeof(u_short),&field3, sizeof(u_int)  );
    
    if (NOTIFY_DATA_VOLUME_WITH_STRAIGHT_UNICAST) { //画像収集ノードに直接ユニキャスト（到達率は混雑状況に応じて低下）
        /* UDPデータグラムソケットの作成 */
        if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
            err(EXIT_FAILURE,"socket() failed");
        
        /* サーバのアドレス構造体の作成 */
        memset(&servAddr, 0, sizeof(servAddr));                   /* 構造体にゼロを埋める */
        servAddr.sin_family      = AF_INET;                      /* インターネットアドレスファミリ */
        servAddr.sin_addr.s_addr = inet_addr(inet_ntoa(ipAddr)); /* サーバのIPアドレス */
        servPort                 = NOTIFY_DATA_VOLUME_PORT;      /* 指定のポート番号があれば使用 */
        servAddr.sin_port        = htons(servPort);              /* サーバのポート番号 */

        sent_suc = sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&servAddr, sizeof(servAddr));
        close(sock);
    } else {                    /* 再送ありの方式 */

        if(NOTIFY_DATA_VOLUME_WITH_HOPBYHOP_ARQ ){ //ホップバイホップUDP再送で送信
            sPInfo.protocolType = UDP;
        } else if ( NOTIFY_DATA_VOLUME_WITH_HOPBYHOP_TCP) {
            sPInfo.protocolType = TCP;
        }
        sPInfo.nextNodeType = PARENT;
        sPInfo.packetType   = AMOUNT_DATA_SEND_MESS;
        sPInfo.ipAddr       = ipAddr; //画像収集ノードのアドレス
        sPInfo.destPort     = NOTIFY_DATA_VOLUME_PORT;
        sPInfo.ackPortMin   = NOTIFY_DATA_VOLUME_ACK_PORT_MY; //中継用ポートと自ノードからの転送用ポートと分ける
        sPInfo.ackPortMax   = NOTIFY_DATA_VOLUME_ACK_PORT_MY;
        sPInfo.packet       = buf;
        sPInfo.packetSize   = sizeof(buf);
        sPInfo.windowSize   = WINDOW_SIZE;
        printf("[SendValueOfData] sending mess to next hop.->sendPacketToNext %d bytes\n"
               ,sPInfo.packetSize);
        sendPacketToNext(sPInfo);
    } 

    if((fp_sendval=fopen(logfilename,"a")) == NULL) {
        printf("FILE OPEN ERROR %s\n",logfilename);
        exit (EXIT_FAILURE);
    }
    gettimeofday(&current_time, NULL);
    day   = (int)current_time.tv_sec;
    usec  = (int)current_time.tv_usec;
    fprintf(fp_sendval
            ,"%d.%06d seq_start %d seq_end %d total %d\n"
            ,day
            ,usec
            ,field1
            ,field2
            ,field3);
    fclose(fp_sendval);
    
    
}


int main(int argc,char *argv[])
{
    
    /*画像ファイルから読み込んだデータの保存用*/
    char               sImage      [BUFFER_READING_IMAGE];
    /*送信するデータ(サイズはシーケンス番号,画像ヘッダサイズ、画像データサイズの和以上にすること)*/
    char              *imageBuffer;
    u_short            imageBufferSize;
    
    struct sockaddr_in servAddr;         /* サーバのアドレス    */
    unsigned short     servPort;         /* サーバのポート番号  */
    struct in_addr     servIP;           /* サーバのIPアドレス  */
    int                sock;
    //hop by hop ARQ使用時
    struct sendingPacketInfo        sPInfo;  //画像データ送信用
    //自ノードでサーバがなく、ただ転送するだけのパケット情報構造体を以下に記述*/
    struct sendingPacketInfo        rIDInfo; //画像データ転送用(relayImageDataInfo)
    struct sendingPacketInfo        rNDInfo; //転送データ量通知メッセージリレー用(relayNotifyData_vol)

    /* thread 作成時のIDとステータス*/  
    pthread_t	thread_id;                     /* 周期制御メッセージ受信用スレッド */
    int	        thread_status;
    pthread_t   vlc_thread_id;                 /* vlcによるwebカメラからの画像取得スレッド*/
    int         vlc_thread_status;
    pthread_t   waitExit_thread_id;            /*終了コード受信用*/
    int         waitExit_thread_status; 
    pthread_t   relayImageData_thread_id;      /*画像データ転送用*/
    int         relayImageData_thread_status;
    pthread_t   relayNotifyData_thread_id;     /*転送データ量通知メッセージ*/
    int         relayNotifyData_thread_status; 

    /* image file */
    int             is_dir_exist = 0;  //画像ファイル保存用ディレクトリが存在するかどうか
    struct stat     directory;

    char            image_name_with_format[256];         /* 転送画像ファイルネーム               */
    char            take_a_snapshot[256];                /*スナップショット取得コマンド*/
    u_short         sequence = 0;                        /* パケットナンバー                     */
    u_short         image_number,diff,image_number_last; /* image_numberは画像データ番号         */
    u_int           value_of_data = 0;                   /* 送信データ量                         */
    int             in;                                  /* 画像データfopen時のファイルディスクリプタだったきがする*/
    u_int           num_of_bytes=0;                      /* 画像データファイル読み込みデータサイズ*/
    /* scanDir */
    char           *dir_name;                            /*USING_FILES時のスキャンディレクトリ名前*/
    struct dirent **namelist;                            /*転送画像データ名前リスト*/
    struct stat     state_of_file;                       /*ファイルの状態取得*/

    int             num_of_files = 0;                    /*ディレクトリ内画像データ数*/

    /* for sending interval*/
    double interval_int, interval_frac;
    int val;                                             /* (1:using non blocking socket, 0: blocking socket) */
    /* for get time */
    struct timeval current_time;
    int            day,usec;
    struct in_addr ipaddr;
    int            hostnum;

    /* for logfile*/
    FILE *fp_data;
    char *logfilename = "logfile_imageSender_sentData.log";
    
    char command[100];
    int  sent_suc=0;

    /* for srand seed*/
    u_int seed;
    /*BMPヘッダ格納用*/
    char bmp_header[BMP_HEADER_SIZE];

    /*BMPヘッダ読み出し実質サイズ*/
    int  read_heder = 0;
    /*ソケットバッファサイズ wmem_max設定コマンド*/
    int  socket_buffer_size_image = SOCKET_BUFFER_SIZE_IMAGE;
    char sysctl_command[100];    
    /*送信ループの所要時間*/
    double time_before_send, time_after_send;
    double loop_int, loop_frac;
    double time_before_send_one_image, time_after_send_one_image;
    double all_sleep_time_one_image; 
    /*１つの画像データ/BUFFER_READING_IMAGEによる分割数（ceilにより切り上げ）*/
    double num_imageblock_raw;  //raw
    double num_imageblock_ceil; //ceilで切り上げ
    double time_next_send;
    double timer_res; //送信処理

   
    
    //packetSenderモジュールの初期化
    initializePacketSender();

    
    /*画像データパケットのサイズ決定、領域確保*/
    if(BMP_HANDLER) {
        imageBufferSize = BMP_APP_PACKET_SIZE;
    } else if (JPG_HANDLER) {
        imageBufferSize = JPG_APP_PACKET_SIZE;
    }
    if ((imageBuffer = (char *)malloc(sizeof(char) * imageBufferSize)) == NULL) {
        err(EXIT_FAILURE, "fail to allocate memory to imageBuffer in main");
    }
    
    /*引数チェック パラメータ初期化*/
    if (USING_FILES) {
        if(argc < 2 || argc > 2) {
            fprintf(stderr,
                    "Usage: %s <Server IP>  <Directory of pictures files>\n"
                    ,argv[0]);
            exit(EXIT_FAILURE);
        } else {
            dir_name = argv[1];
        }
    }
   
    /*初期周期広告後の初回の画像転送時にランダムウェイトするためのシード値をIPアドレス最下位8bitに設定*/
    ipaddr = GetMyIpAddr(WIRELESS_DEVICE);
 
    /*画像ファイル保存用ディレクトリ作成*/
    if ((is_dir_exist = stat(inet_ntoa(ipaddr), &directory)) != 0 ) { /*ディレクトリ存在作成*/
        if ( mkdir( inet_ntoa(ipaddr),
                    S_IRUSR | S_IWUSR | S_IXUSR |         /* rwx */
                    S_IRGRP | S_IWGRP | S_IXGRP |         /* rwx */
                    S_IROTH | S_IXOTH | S_IXOTH) != 0) {  /* rwx */
            err(EXIT_FAILURE, "dir %s is not created\n", inet_ntoa(ipaddr));
        }
    }

    if( USING_CAMERA) {
        /*VLCによるWEBカメラからの画像取得スレッド作成*/
        vlc_thread_status = pthread_create(&vlc_thread_id,
                                           NULL,
                                           execVLC,
                                           NULL);
        if( vlc_thread_status != 0){ /*スレッド作成失敗*/
            fprintf(stderr,"pthread_create : %s is failed",strerror(vlc_thread_status));
            exit(EXIT_FAILURE);
        }
        /*
         * スナップショットコマンド・送信画像ファイル名
         * (生データのままでも送信可能だが、収束周期が大きすぎると実験が終わらないので)
         * ピクセル数を落としてファイルサイズを減少させている
         * 生データでよければ cp 元ファイル名　送信ファイル名 にする
         */
        if(BMP_HANDLER) {
            sprintf(image_name_with_format, "%s.%s",IMAGE_FILE_NAME,BMP_IMAGE_FORMAT);
            sprintf(take_a_snapshot,
                    "cp %s.%s %s"
                    ,SNAP_SHOT_FILE_NAME
                    ,BMP_IMAGE_FORMAT
                    ,image_name_with_format
            );
            /* sprintf(take_a_snapshot,  */
            /*         "convert -resize 640  -colors 65 -quality 100 -verbose %s.%s %s" */
            /*         ,SNAP_SHOT_FILE_NAME */
            /*         ,BMP_IMAGE_FORMAT */
            /*         ,image_name_with_format */
            /* ); */
        } else if (JPG_HANDLER) {
            sprintf(image_name_with_format, "%s.%s",IMAGE_FILE_NAME,JPG_IMAGE_FORMAT);
            sprintf(take_a_snapshot, 
                    "cp %s.%s %s"
                    ,SNAP_SHOT_FILE_NAME
                    ,JPG_IMAGE_FORMAT
                    ,image_name_with_format
            );
            
        }

    } else if (USING_FILES) {
        /* 送信画像が保存されているディレクトリのスキャン*/
        if((num_of_files
            = scandir(dir_name, &namelist, scanDirFilter, alphasort)) == -1) {
            err(EXIT_FAILURE, "%s", dir_name);
        }
    }
    /*初期周期広告の受信待ち(ブロッキング)*/
    printf("[Wait for receiving Beacon signal]\n");
    servIP = waitingBeacon();
   
    //ビーコン受信
    servPort = IMAGE_DATA_PORT;
    printf("[BEACON RECEIVED] image data will sent to %s. initial period %lf...\n"
           ,inet_ntoa(servIP)
           ,SendingInterval);
    

    sprintf(sysctl_command, "sudo sysctl -w net.core.wmem_max=%d\n",socket_buffer_size_image);
    system(sysctl_command); 
    sprintf(sysctl_command, "sudo sysctl -w net.core.wmem_default=%d\n",socket_buffer_size_image);
    system(sysctl_command); 

    /* APPでの中継スレッド起動 */
    /* 画像データ */
    if(IMAGE_DATA_TRANSFER_WITH_STRAIGHT_UNICAST) { /* UDPユニキャスト(再送なし)はこの関数で送信処理を行う */
        //non blocking socket設定 (val==0 でブロッキングモード) 
        val = 1;
        //画像データ送信用ソケットを作成
        if((sock = socket(PF_INET,SOCK_DGRAM, 0)) < 0) {
            err(EXIT_FAILURE, "fail of socket creation\n");
        }
        ioctl(sock, FIONBIO, &val); 
        setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)&socket_buffer_size_image, sizeof(socket_buffer_size_image));
        memset(&servAddr, 0, sizeof(servAddr));
        servAddr.sin_family      = AF_INET;
        servAddr.sin_addr        = servIP;
        servAddr.sin_port	     = htons(servPort);
    } else  {                   /* 再送ありの場合、この関数では送信処理せずpacketSenderがsocketを作成し、送信 */
        rIDInfo.nextNodeType    = PARENT;
        rIDInfo.packetType      = IMAGE_DATA_PACKET;
        rIDInfo.protocolType    = UDP;
        rIDInfo.ipAddr          = servIP;  //画像収集ノードのアドレスがdestとなる次ホップ
        rIDInfo.destPort        = IMAGE_DATA_PORT; //宛先ポート
        rIDInfo.dataReceivePort = IMAGE_DATA_PORT; //リレー用ポート
        rIDInfo.ackPortMin      = IMAGE_DATA_ACK_PORT_MY; 
        rIDInfo.ackPortMax      = IMAGE_DATA_ACK_PORT_MY;
        //リレー用スレッド作成
        if((relayImageData_thread_status
            = pthread_create(&relayImageData_thread_id, NULL, relayPacket, &rIDInfo)) != 0) {
            err(EXIT_FAILURE, "fail to create relayImageData thread (main)\n");
        }
    }
    
    /* 転送データ量通知メッセージの送信方式 */
    if(NOTIFY_DATA_VOLUME_WITH_HOPBYHOP_ARQ
       || NOTIFY_DATA_VOLUME_WITH_HOPBYHOP_TCP ) { //転送データ量通知メッセージがhop by hopARQで行われる場合
        rNDInfo.nextNodeType    = PARENT;
        rNDInfo.packetType      = AMOUNT_DATA_SEND_MESS;
        /* プロトコルタイプ */
        if(NOTIFY_DATA_VOLUME_WITH_HOPBYHOP_ARQ) {
            rNDInfo.protocolType    = UDP;
        } else if (NOTIFY_DATA_VOLUME_WITH_HOPBYHOP_TCP) {
            rNDInfo.protocolType    = TCP;
        }
        rNDInfo.ipAddr          = servIP;  //画像収集ノードのアドレスを指定すると、方向の次ホップに送信される
        rNDInfo.destPort        = NOTIFY_DATA_VOLUME_PORT;
        rNDInfo.dataReceivePort = NOTIFY_DATA_VOLUME_PORT;
        rNDInfo.ackPortMin      = NOTIFY_DATA_VOLUME_ACK_PORT_MY;
        rNDInfo.ackPortMax      = NOTIFY_DATA_VOLUME_ACK_PORT_MY;
        
        if((relayNotifyData_thread_status
            = pthread_create(&relayNotifyData_thread_id, NULL, relayPacket, &rNDInfo)) != 0) {
            err(EXIT_FAILURE, "fail to create relayImageData thread (main)\n");
        }

    } 

    /*共通スレッド*/
    /*(waitingBeaconのあとにスレッド作成すること)転送周期制御メッセージ受信用スレッド*/
    thread_status=pthread_create(&thread_id,
                                 NULL,
                                 recvIntervalControlMessage,
                                 NULL);
    if( thread_status != 0){
        fprintf(stderr,"pthread_create : %s is failed",strerror(thread_status));
        exit(EXIT_FAILURE);
    } 
    
    /*終了コード受信用スレッド*/
    waitExit_thread_status  =pthread_create(&waitExit_thread_id,
                                            NULL,
                                            waitExitCode,
                                            NULL);//中継用フィルタに画像収集ノードのアドレスを使用
    if( waitExit_thread_status != 0){
        fprintf(stderr,"pthread_create : %s is failed",strerror(waitExit_thread_status));
        exit(EXIT_FAILURE);
    } 
    //自身IPアドレス(v4アドレス==4bytes)と端末現在時刻(usec)をシードとするので端末毎にランダム値を取る
    memcpy(&seed, &ipaddr, sizeof(u_int));
    
    gettimeofday(&current_time, NULL);
    //day   = (int)current_time.tv_sec;
    usec  = (int)current_time.tv_usec;
    seed = seed + usec;

    srand(seed);

    interval_frac = modf((FIXED_WAIT_TIME+SendingInterval*rand()/RAND_MAX), &interval_int);
   
    /*ランダムウェイト[0,SendingInterval)*/
    sleep((unsigned int)interval_int);
    usleep (interval_frac*1000000);

    /*Sending images*/
     /*画像の送信回数*/
    if (num_of_files > NUM_SEND_PICTURES ) 
      num_of_files = NUM_SEND_PICTURES;

    //for(image_number=0; image_number < NUM_SEND_PICTURES; image_number++) {
    for(image_number=0, image_number_last=0; /*終了コード受信するまで送信*/ ; image_number++) {

        if (USING_FILES) {
            strcpy(image_name_with_format, dir_name);
            strcat(image_name_with_format, namelist[image_number]->d_name);
            free(namelist[image_number]);
        }

        /*スナップショット取得*/
        system(take_a_snapshot);
        if (stat  (image_name_with_format, &state_of_file) != 0) {
            err(EXIT_FAILURE, "[main] cannot get file status.\n");
        }
        //分割数
        num_imageblock_raw  = (double)state_of_file.st_size/BUFFER_READING_IMAGE;
        num_imageblock_ceil = ceil(num_imageblock_raw);
        
        if ((in = open(image_name_with_format, O_RDONLY)) < 0)
            err(EXIT_FAILURE, "read erro at file %s. ",image_name_with_format);

        if (BMP_HANDLER) {
            /*BMPファイルのヘッダ読み込み*/
            memset (bmp_header, 0, BMP_HEADER_SIZE);
            if( (read_heder = read(in, bmp_header, BMP_HEADER_SIZE)) < 0) {
	
                err(EXIT_FAILURE, "an error occurs while reading bmp file header\n");
            }
            //value_of_data += read_heder;
      
        }
     

        /*1枚の画像フラグメント転送ループ*/
        timer_res = 0; //スリープ用パラメータ
        sequence = 0;  //パケットナンバー初期化
        all_sleep_time_one_image = 0;
        while (1) { //1画像ファイルをMAXIMUM_SEGMENT_SIZEずつ読み込み、送信する。送信し終わったらループを抜ける
            
            gettimeofday(&current_time, NULL);
            day   = (int)current_time.tv_sec;
            usec  = (int)current_time.tv_usec;
            time_before_send = day + (double)usec/1000000; //処理開始時間保存

            if (sequence == 0 ) {

                time_before_send_one_image = time_before_send; //1毎の画像の送信前時間
                printf("[%d.%06d] image%03d (%d bytes, %.0lf fragments.  %.5lf sec/frag) will be sent...\n"
                       ,day
                       ,usec
                       ,image_number
                       ,state_of_file.st_size
                       ,num_imageblock_ceil
                       ,SendingInterval/num_imageblock_ceil
                );
            }
    	    memset(sImage, 0, BUFFER_READING_IMAGE); /*0セット*/
           
            if((num_of_bytes = read(in, sImage, BUFFER_READING_IMAGE)) < 0) { /*ファイル読み込みエラー*/
                err(EXIT_FAILURE, "read erro at file %s. "
                    ,image_name_with_format);
		
            } else {
	      
                // 画像データ送信処理
                if(BMP_HANDLER) {
                    memset(imageBuffer, 0, BMP_APP_PACKET_SIZE); /*0セット*/
                }else if (JPG_HANDLER) {
                    memset(imageBuffer, 0, JPG_APP_PACKET_SIZE);
                
                }
                memcpy(imageBuffer, 
                       &ipaddr, 
                       sizeof(struct in_addr)); //送信元IPアドレス
                memcpy(imageBuffer+sizeof(struct in_addr), 
                       &image_number, 
                       sizeof(u_short));        //画像データ番号

                memcpy(imageBuffer+sizeof(struct in_addr)+sizeof(u_short), 
                       &sequence, 
                       sizeof(u_short));       //パケット番号
                if (BMP_HANDLER) {
                    memcpy(imageBuffer+sizeof(struct in_addr)+sizeof(u_short)+sizeof(u_short),
                           bmp_header,
                           BMP_HEADER_SIZE);   //BMPファイルヘッダ
                    memcpy(imageBuffer+sizeof(struct in_addr)+sizeof(u_short)+sizeof(u_short)+BMP_HEADER_SIZE,
                           sImage,
                           num_of_bytes);      //BMP画像データ本体
                    
                    
                    /*パケット送信*/
                    if (IMAGE_DATA_TRANSFER_WITH_STRAIGHT_UNICAST ) {
                        /*送信データサイズ（第３引数）はreadでの読み出しサイズ＋シーケンス番号、
                         *ファイルヘッダである.APP_PACKET_SIZEに設定しないこと*/
                        sent_suc = sendto(sock, imageBuffer
                                          ,num_of_bytes+sizeof(struct in_addr)+2*sizeof(u_short)+BMP_HEADER_SIZE, 0
                                          ,(struct sockaddr *)&servAddr, sizeof(servAddr));
                    }else {
                        sPInfo.nextNodeType = PARENT;
                        sPInfo.packetType   = IMAGE_DATA_PACKET;
                        sPInfo.ipAddr       = servIP;  //画像収集ノードのアドレスがdestとなるの次ホップ
                        sPInfo.destPort     = IMAGE_DATA_PORT;
                        sPInfo.protocolType = UDP;

                        //        sPInfo.dataReceivePort
                        sPInfo.ackPortMin   = IMAGE_DATA_ACK_PORT_MY;
                        sPInfo.ackPortMax   = IMAGE_DATA_ACK_PORT_MY;
                        sPInfo.packet       = imageBuffer;
                        sPInfo.packetSize   = num_of_bytes+sizeof(struct in_addr)+2*sizeof(u_short)+BMP_HEADER_SIZE;
                        sPInfo.windowSize   = WINDOW_SIZE;
                        printf("[main] call sendPTN imgnum %d seq %d , %d bytes\n",image_number, sequence,sPInfo.packetSize);
                        sendPacketToNext(sPInfo);

                    }

                }else if (JPG_HANDLER) {
                    memcpy(imageBuffer+sizeof(struct in_addr)+sizeof(u_short)+sizeof(u_short),
                           sImage,
                           num_of_bytes);   //画像データ本体
                    //画像データ送信
                    if (IMAGE_DATA_TRANSFER_WITH_STRAIGHT_UNICAST ) {
                    sent_suc = sendto(sock, imageBuffer, num_of_bytes+sizeof(struct in_addr)+2*sizeof(u_short), 0,
                                      (struct sockaddr *)&servAddr, sizeof(servAddr));
                    } else {
                        sPInfo.nextNodeType = PARENT;
                        sPInfo.packetType   = IMAGE_DATA_PACKET;
                        sPInfo.ipAddr       = servIP;  //画像収集ノードのアドレスがdestとなるの次ホップ
                        sPInfo.destPort     = IMAGE_DATA_PORT;
                        //        sPInfo.dataReceivePort
                        sPInfo.ackPortMin   = IMAGE_DATA_ACK_PORT_MY;
                        sPInfo.ackPortMax   = IMAGE_DATA_ACK_PORT_MY;
                        sPInfo.packet       = imageBuffer;
                        sPInfo.packetSize   = num_of_bytes+sizeof(struct in_addr)+2*sizeof(u_short);
                        sPInfo.windowSize   = WINDOW_SIZE;
                        sendPacketToNext(sPInfo);
                    }
                }
	      
                value_of_data += num_of_bytes;
                gettimeofday(&current_time, NULL);
                day   = (int)current_time.tv_sec;
                usec  = (int)current_time.tv_usec;
                if((fp_data=fopen(logfilename,"a")) == NULL) {
                    printf("FILE OPEN ERROR %s\n",logfilename);
                    exit (EXIT_FAILURE);
                }
                /*	      printf("%d.%06d %s's_data volume %d image_number %d sequecne %d\n"
                          ,day
                          ,usec
                          ,image_name_with_format
                          ,num_of_bytes
                          ,image_number
                          ,sequence);*/
                //ログファイルに追記
                fprintf(fp_data
                        ,"%d.%06d %s's_data volume %d image_number %d sequence %d\n"
                        ,day
                        ,usec
                        ,image_name_with_format
                        ,num_of_bytes
                        ,image_number
                        ,sequence
                );
                fclose(fp_data);

                //パケットシーケンス番号更新
                sequence++;
                //                printf("image data enqueue end\n");
            }
         
            if (sequence  == num_imageblock_ceil) { //１枚分のデータ送信終了
                //デバッグ
              
                /* time_after_send_one_image = day + (double)usec/1000000; */
                
                /* printf("[%d.%06d] image%03d has been sent (elapsed %.10lf (sum sleep %.10f) sec. now Interval %lf allfragments %.0lf)\n" */
                /*        ,day */
                /*        ,usec */
                /*        ,image_number */
                /*        ,time_after_send_one_image-time_before_send_one_image */
                /*        ,all_sleep_time_one_image */
                /*        ,SendingInterval */
                /*        ,num_imageblock_ceil */
                /* ); */
                //送信データ量通知メッセージの送信タイミング判定
                diff = image_number - image_number_last;
            
               
                if (diff  >= INTERVAL_SEND -1) {  //送信タイミング
                    SendValueOfData(servIP, image_number_last, image_number, value_of_data ); 
                    printf("[sending message] imageNumber %3d through %3d amount_of_sent %10d\n"
                           ,image_number_last
                           ,image_number
                           ,value_of_data);

                    image_number_last = image_number + 1;
                    value_of_data = 0;
                }
	      
              
                // 送信画像データ・バックアップ作成
                if (SAVE_PICTURES) { //ディレクトリに保存
                    if(BMP_HANDLER) {
                        sprintf(command, 
                                "cp %s %s/%05d.%s"
                                ,image_name_with_format
                                ,inet_ntoa(ipaddr)
                                ,image_number
                                ,BMP_IMAGE_FORMAT);
                    }else if (JPG_HANDLER) {
                        sprintf(command, 
                                "cp %s %s/%05d.%s"
                                ,image_name_with_format
                                ,inet_ntoa(ipaddr)
                                ,image_number
                                ,JPG_IMAGE_FORMAT);                    
                    }
                } else { //最新の１枚だけ保存する。
                   if(BMP_HANDLER) {
                        sprintf(command, 
                                "cp %s %s/most_resent.%s"
                                ,image_name_with_format
                                ,inet_ntoa(ipaddr)
                                ,BMP_IMAGE_FORMAT);
                    }else if (JPG_HANDLER) {
                        sprintf(command, 
                                "cp %s %s/most_resent.%s"
                                ,image_name_with_format
                                ,inet_ntoa(ipaddr)
                                ,JPG_IMAGE_FORMAT);                    
                    }
                }
                
                system (command);//画像作成
             
                break;


            } else { //次回のパケット送信までのスリープ時間設定


                /*パケット送信間隔*/
                gettimeofday(&current_time, NULL);
                day   = (int)current_time.tv_sec;
                usec  = (int)current_time.tv_usec;
            
                time_after_send = day + (double)usec/1000000;
                if (PeriodChange == 1) { /*周期変化メッセージ受信時にランダムウェイト*/
                    time_next_send  = (SendingInterval / num_imageblock_ceil-(time_after_send-time_before_send))*rand()/RAND_MAX;
                    time_next_send  = time_next_send + timer_res;
                    interval_frac   = modf(time_next_send , &interval_int);
                    PeriodChange    = 0;
               
                    /* printf("[%d.%06d] [NEW interval set]sleep %.5lf sec (if < 0 nonsleep!)\n" */
                    /*        ,day */
                    /*        ,usec */
                    /*        ,time_next_send); */
                } else {
                    time_next_send  = (SendingInterval / num_imageblock_ceil-(time_after_send-time_before_send));
                    time_next_send  = time_next_send + timer_res;
                    interval_frac   = modf(time_next_send, &interval_int);
                
                }
                printf("[%d.%06d] (status %d) send to %s sleep %.10lf sec ( timer res %.10lf intraw %.10lf for loop %.10lf sec exceeds)\n"
                       ,day
                       ,usec
                       ,sent_suc
                       ,inet_ntoa(servIP)
                       ,time_next_send
                       ,timer_res
                       ,SendingInterval / num_imageblock_ceil
                       ,time_after_send-time_before_send
                );

                if (time_next_send < 0) { //送信処理時間が送信間隔を超過した時、次回の処理時間に持込
                    timer_res = time_next_send;
                
                } else  {
                    timer_res = 0;
                    all_sleep_time_one_image += time_next_send; //sleep時間デバッグ
                    sleep((unsigned int)interval_int);
                    usleep (interval_frac*1000000);

                }
            }
           
        }
        close(in); //画像ファイルをクローズ

        
      
        /*printf("[Time elapsed %10lf second]\n",after_loop-before_loop);*/
	
        /*waiting for elapsing a sending period (ただしループにかかった所要時間を除く*/
        /*        if (PeriodChange == 1) { 
            interval_frac = modf((SendingInterval-(after_loop-before_loop))*rand()/RAND_MAX, &interval_int);
            PeriodChange = 0;
        } else {
            interval_frac = modf(SendingInterval-(after_loop-before_loop), &interval_int);
        }*/
        /*次の送信時刻まで待機*/
        /*printf("[main] wait for elapsing %lf second\n",(SendingInterval-(after_loop-before_loop)));
        sleep((unsigned int)interval_int);
        usleep (interval_frac*1000000);
        */

    } // [end of sending all image file.]

    if (USING_CAMERA) {
        // プロセスが残るので、ここでkill
        system ("killall vlc\n"); 
    }

    return 0;
}

