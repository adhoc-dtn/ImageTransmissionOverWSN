/* Time-stamp: <Wed Dec 25 14:22:27 JST 2013> */

/* TODO */
/* X1 いろいろきれいにする
 * X2 周期制御部分を、PID制御風にパラメータ更新すると、収束が早くなるはず
 * 
 * serverImageReceiver.c
 *
 *--用意するもの--
 * 経路表(自ノードを根とするツリー状ネットワークにすること)
 * /proc/net/routeを参照して隣接ノードを取得するperlプログラム　./get_next_hop.pl
 * (このプログラムは隣接ノードが重複していてよい)
 * --概要--
 *         imageSender_multihop.c（クライアント）から送信されてくる画像データパケットを受信し、
 *         １枚の画像データに復元する。またクライアントから転送データ量通知メッセージを受信し、
 *         定期的に受信成功率を算出する。
 *         ADAPTIVE : 受信成功率に応じて、クライアント側の転送周期を最適化する
 *         STATIC   : 初期周期広告で最後まで画像データを送信する。
 *
 *+++ 説明　+++
 *(はじめて使う人はよく読んでください)
 * 
 * (I) クライアントでの画像データ送信方法
 *     クライアントは画像データをMAXIMUM_SEGMENT_SIZE(クライアント側マクロ定義)に分割して
 *     周期的に送信する。画像データは
 *       (i)  JPEG
 *       (ii) BMP(WindowsV3)
 *     の２パターンが受信可能(マクロ定義)
 *
 * 各転送パターンを選択したとき、クライアントが送信する１パケットのフィールドは以下のように設定される
 * (i) クライアントがJPEGデータを転送する場合 (#define JPEG_SENDER)の場合
 *    #define JPG_HANDLERを定義する 各フィールドは
 *  ____________________________________________________________
 * |  画像データ番号 | パケット番号  | ペイロード(JPEG)           |
 * |   (2byte)      | (2byte)      |  (MAX 1400byte)           |
 *  ------------------------------------------------------------
 *
 * (ii) BMP転送時 (#define BMP_SENDER)の場合 (#define BMP_HANDLER)の場合
 *     #define BMP_HANDLERを定義する 各フィールドは
 * _____________________________________________________________________________________________________________
 * |   送信元IPv4アドレス   |  画像データ番号 | パケット番号  | BMP(WindowsV3)ヘッダ  |      ペイロード(JPEG　)     |
 * |    (4bytes  )         |   (2byte)      | (2byte)      |  (54 byte　)          |     (可変長 MAX 1350 byte)   |
 * -------------------------------------------------------------------------------------------------------------
 * 
 * (II) 受信する転送データ量通知メッセージのフィールドは以下のように規定される
 *
 * _____________________________________________________________________________________________________________
 * |   送信元IPv4アドレス    |シーケンス番号(転送はじめ)|シーケンス番号(転送終わり)|          合計転送データ量        |
 * |    (4bytes  )          |           (2 bytes)    |        (2 bytes)        |            (4 bytes)           |
 * --------------------------------------------------------------------------------------------------------------
 *
 * (III)送信する初期周期広告のフィールドは以下のように規定する
 *  i) ブロードキャスト時
 *  __________________________ __________________________
 * |     画像収集ノードアドレス    |       初期周期           |
 * |   (struct in_addr 4 bytes)   |     (float 4 bytes)     |
 * ---------------------------------------------------------
 *  ii) ユニキャストwith アプリケーションARQ
 * _____ ___________________________________________________
 * |     画像収集ノードアドレス    |       初期周期          |
 * |   (struct in_addr 4 bytes)  |     (float 4 bytes)     |
 * -------------------------------------------------------------------------------
 *
 * (IV)送信する周期制御メッセージは各フィールドは以下のように規定している
 * i)ブロードキャスト時
 * _____________________________________________
 * | シーケンス番号     |          新周期         |
 * | (u_short 2 bytes) |       (float 4 bytes)  |
 * ----------------------------------------------
 * ii)ユニキャストwith アプリケーションARQ
 * ____________________________________________________________________
 * | ACK受信用ポート番号  | シーケンス番号     |          新周期       |
 * | (u_short 2 bytes)   | (u_short 2 bytes) |     (float 4 bytes)  |
 * ---------------------------------------------------------------------
 *
 *
 * (V) Auto Repeat Request (ARQの有無)
 *    初期周期広告と制御メッセージ（以下、両方ともメッセージと呼称）
 *    の送信方法は、以下の３パターンが利用可能である
 *    (i) ブロードキャスト(MACブロードキャストなのでリンク層でのARQなし) CONTROL_METHOD_BROADCAST
 *        各ノードはブロードキャストによって次ホップノードにメッセージを転送する
 *        各ノードはメッセージを受信したら、同じメッセージを再ブロードキャストする
 *    (ii) ユニキャスト(MACでのARQ利用方式)     [CONTROL_METHOD_UNICAST_WITH_ARQ, ACK_RETRY_MAX==0]
 *        各子ノードのユニキャストアドレスにメッセージを送信する
 *    (iii) ユニキャスト with ARQ at APP
 *        (ii)の場合加えて、隣接ノード間でアプリケーションレベル(本プログラム)
 *            でACK再送制御する CONTROL_METHOD_UNICAST_WITH_APPLV_ARQ
 *        タイムアウトをACK_TIMEOUT、最大再送回数をACK_RETRY_MAX(>=1)で規定
 *
 *
 *+++ 使い方 +++
 *----------------------------------------------------------------------------------------------------
 * 全端末画像送信周期制御プログラム(ビーコンによる周期設定機能付き)
 *   機能: imageSenderによって画像ファイルを受信し、受信ログを[port(受信ポート番号).log]と追記する
 *         また、低受信率/高受信率を判断すると画像送信周期制御メッセージを送信。送信周期延長/短縮を行う
 *         
 *        compile : $ gcc serverImageReceiver
 *                              -lpthread -lrt -o serverImageReceiver
 *
 * 前回受信ログ/jpgファイル削除: $ rm port*; rm [IPaddr] -rf
 *        execute : $ ./serverImageReceiver (name wlan interface: e.g. wlan0)
 *           (initial sending period: e.g. 15.0) (timer of calculate racev ratio: e.g. 30 ( means settting 30 x initial sending period)
 *-----------------------------------------------------------------------------------------------------
 */

#include "common.h"

/*受信成功率算出サイクル経過後に、プログラム終了*/
/*実験に必要なサイクルより少し大きめに取るとよい（最後のサイクルで周期制御メッセージ転送途中で終了するから）*/
#define NUM_CYCLE_FOR_EXIT     (50)

/* 周期制御パラメータ*/
/*周期延長判断用 受信率しきい値*/
#define GAMMA1                 (0.90)
/*周期短縮判断用 受信率しきい値*/
#define GAMMA2                 (0.98)
/* 周期延長パラメータ (1+alpha)*current_sending_interval のように設定 */
#define ALPHA                  (0.1)
/* 周期短縮パラメータ (1- beta)*current_sending_interval のように設定 */
#define BETA                   (0.1)

/*ビーコン・制御メッセージ宛先(ブロードキャストアドレス)*/
#define  BROADCAST_IP          "192.168.50.255"
/*ビーコンIP(ロストしないようにしている) 本実験では無線となる*/
#define  BEACON_IP             "192.168.30.255"

/*#define BEACON_IP          BROADCAST_IP */


 /* 輻輳検知条件 検知方式 (STATIC_PERIODの受信成功率のログにも反映)*/
enum {
    WHOLE_CONGESTION_DETECT  = 1, /* すべての受信データ量に対してすべての送信データ量*/
    MIN_CONGESTION_DETECT    = 0, /* 各端末ごとに受信データ量/送信データ量を算出、その最小値を取る*/
};

/*受信されたパケットを挿入する*/
/*ヘッドに近いほうが新しいデータ(番号大)であり、先頭が古いデータ(番号小)となる。*/
struct one_packet_data {
    int            day;   //gettimeofdayの受信時刻(整数部分)
    int            usec;  //上記の小数部分
    struct in_addr ip_src;
    u_short        image_number;
    u_short        sequence_number;
    u_short        data_size;
    struct one_packet_data *next;
};
/*受信したパケットのリスト mutex*/
struct one_packet_data One_Packet_Data_Head;
pthread_mutex_t        one_packet_data_list_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * head.next以下、ヘッドに近いほうが古いデータ(番号小の傾向)、先頭が新しいデータ(番号大)となる
 * 画像データ(1枚分 要素の重複なし)に関する情報を格納し、リストにする   
 * [注意] list_sizeは、リストのソートが必要なときに使用する。ソートの必要がなければ使用する必要はない
 */
struct one_image_data {
    u_int             list_size_img_data; /*当リストの大きさ(ヘッドのみ管理可能 他の要素では操作しない) */
    struct in_addr    ip_src;         /* データ送信元IPアドレス                                     */
    u_short           image_number;   /* 画像データ番号                                             */
    u_int             image_size;     /* 画像データサイズ                                             */
    struct one_packet_data head;      /* 同IPアドレス、画像データ番号の情報リストのヘッド next以降にサイズ  */
    u_int             list_size_pac_data; /*上記リストの大きさ*/
    struct one_image_data  *next;     /* next pointer   */
};
/*今回受信パケットに関するリスト*/
struct one_image_data One_Image_Data_Head; 
/*one_packetリストの操作時には上記mutexを使用すること*/

/*ヘッドに近いほうが新しいデータであり、先頭が古いデータとなる。*/

/* 送信データ量通知メッセージ(順方向環状リスト) */
struct send_data_val_mess  {
    struct in_addr  ip_src;             /* パケット送信元IPアドレス                   */
    u_short         s_start;            /* 画像データ番号 start                      */
    u_short         s_end;              /* 画像データ番号 end                        */
    u_int           size;               /* 送信画像データサイズ合計(ヘッダを除く合計)  */
    struct send_data_val_mess *next;    /* next pointer(ヘッドから末端の方向)         */
    struct send_data_val_mess *prev;    /* prev pointer(末端からヘッドの方向)         */
};
/*ヘッド*/
struct send_data_val_mess Send_Data_Val_Mess_Head;
pthread_mutex_t send_data_val_mess_list_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 各ノードでの受信成功率算出用リスト */
struct recv_data_ratio {
  struct in_addr        ip_src;           /* パケット送信元IPアドレス */
  long long int    amount_recv;           /* 受信データ量合計         */ 
  long long int    amount_send;          /* 送信データ量合計         */
  struct recv_data_ratio *next;
};

struct recv_data_ratio Recv_Data_Ratio_Head;


/* 上のリスト操作時の排他制御用ロック mutex */
pthread_mutex_t next_hop_addr_list_mutex = PTHREAD_MUTEX_INITIALIZER;
/*----リスト定義ここまで--------*/

/*転送データ量通知メッセージの受信*/
void ReceiveImageDataValueMessage ();
/*受信成功率算出*/
void CalcDataRecvRate             (int signum);
/*jpgファイルサイズ・シーケンス番号抽出受信*/
void *ReceiveImageFile            (void *udp_port);
/*周期制御メッセージ送信(パケット初期化まで行う)*/
void SendingPeriodControl         (float param );
/*初期周期広告(パケット初期化まで行う)*/
void SendingBeacon       (char *dev_for_communication, float param);


/*プログラム終了コード送信*/
void SendingExitCode     (int code);
/*転送データ量通知メッセージを受信*/
void *makeSendValDataListfromTCPpack (void *param);

/* 初期化 */
extern void initializePacketSender();
/*初期周期広告、周期制御メッセージ送信*/
extern void SendingControlMessToChild (int     my_port_min,
                                       int     my_port_max,
                                       int     dest_port,
                                       char    buffer[],
                                       u_short buffer_size, 
                                       char   *addr_filter);

/*指定インタフェースのIPアドレス取得*/
extern struct in_addr GetMyIpAddr     (char* device_name);

static void mergeSort_imageData  (struct one_image_data  *head, u_int size);
static void merge_imageData      (struct one_image_data  *list1_head, u_int size_left,
                                  struct one_image_data  *list2_head, u_int size_right,
                                  struct one_image_data  *original_head);

static void mergeSort_packetData (struct one_packet_data  *head, u_int size);
static void merge_packetData     (struct one_packet_data  *list1_head, u_int size_left,
                                  struct one_packet_data  *list2_head, u_int size_right,
                                  struct one_packet_data  *original_head);

/* timer  */
struct sigaction act, oldact;
timer_t tid;
struct itimerspec itval;

/* 初期送信周期       (入力値)*/
float SendingInterval;
/* regulation period (入力値)*/
int   NumImages;


/* thread id of main.*/
static int Main_Thread_ID;

int
main(argc, argv)
    int argc;
    char *argv[];
{
    /* thread */
    pthread_t	thread_mess;
    int	        receive_message;
    
    pthread_t	thread_jpg;
    int         jpg_receiver;    
    int         port;
    void        *result;
    char        *wlan_device;
    sigset_t    signal_set;
    int         signal_port_open      = SIGNAL_PORT_OPEN;
    int         signal_del_item_image = SIGNAL_DEL_ITEM_IMAGE;
    /*プロセスがブロックするシグナルの登録*/
    sigemptyset     ( &signal_set );
    sigaddset       ( &signal_set, signal_port_open );
    sigaddset       ( &signal_set, signal_del_item_image);
    pthread_sigmask ( SIG_BLOCK, &signal_set, 0 );
    Main_Thread_ID = pthread_self();

    //paketSenderモジュールの初期化
    initializePacketSender();
    
    //JPG_RECEIBER, BMP_HANDLERの両者の同時定義の禁止
    if (JPG_HANDLER &&  BMP_HANDLER) {
      err (EXIT_FAILURE, "Definning JPG_HANDLER and BMP_HANDLER is prohibitted. exit. ");
    }
      
    //引数確認
    if((argc < 4) || (argc > 4)) {
      fprintf(stderr,
	      "Usage: %s <wlan device> <initial image acquisition period(float)> <num of images to calc recv ratio(float)>\n"
	      ,argv[0]);
      exit(EXIT_FAILURE);
    } else {
      wlan_device     = argv[1];
      SendingInterval = atof(argv[2]);
      NumImages       = atof(argv[3]);
    }
    
    //jpgファイルの受信用スレッド作成
    port              = IMAGE_DATA_PORT;
    if ( (jpg_receiver = pthread_create(&thread_jpg,
                                        NULL,
                                        ReceiveImageFile,
                                        &port) ) != 0) {
        err(EXIT_FAILURE, "error jpg_receiver thread create in main\n");
    }
    //waiting for port open.
    
    sigwait(&signal_set, &signal_port_open); 
    printf("[mainthread %#x] signal received. Now SendingBeacon!\n",pthread_self());
    /*初期周期広告*/
    SendingBeacon (wlan_device, SendingInterval);      
    
    /*転送データ量通知メッセージ受信待ち状態に遷移*/
    ReceiveImageDataValueMessage();
   
    //ここから下の行は実行されない
    return (0);
}


/*jpgファイルサイズ・シーケンス番号抽出受信*/
void *ReceiveImageFile ( void *udp_port)
{
    /* Paramters listed below are used for esstablishing UDP server.*/
    unsigned short *port     = (unsigned short *)udp_port; 
    int srcSocket;  

    struct sockaddr_in srcAddr;
    struct sockaddr_in clientAddr;     
    unsigned int       clientAddrLen;  
    struct in_addr     clientIP;
    char               clientIPstr[20];
    int                clientIPLen;
    //画像の保存ディレクトリ作成用
    int  status;
    int  numrcv                 = 0;     /* 受信データサイズ  */
    int  pacSize;                        /* 受信パケットサイズ測定用 */
    u_short image_number;                /* 画像データの画像番号                   */
    u_short sequence;                    /* パケット番号                           */
    u_short old_sequence        = 0;     /* 前回受信画像番号                       */
    u_short jpg_first_data      = 0;     /* データの最初の2 byte格納用             */
    u_short jpg_last_data       = 0;     /* データの最後の2 byte格納用              */

    char    data_buffer  [SIZE_DATA_BUFFER];    /* 受信用バッファ                   */
    char    tmp_filename [50];                  /* TEMPファイル名前                 */
    char    file_header  [BMP_HEADER_SIZE];    /* BMPヘッダサイズ                  */
    u_int   bmp_image_size;                     /* BMPファイルサイズ(ヘッダから抽出) */
    u_short header_index;                       /* BMPヘッダのインデックス*/
    u_short header_index_num;                    /*上記の増分*/
    //[受信画像データ(今回)のリスト]
    // 項目新規確保用
    struct one_packet_data *data;

    //探索結果(0: ヒットなし=新規挿入 1: ヒットあり、ファイル書き込み)
    u_short hit_buffer_data = 0;
  
    int value_data;
    FILE *fp;
    char filename[100];
  
    /* get time */
    struct timeval current_time;
    int            day,usec;    
    /* output file */
    FILE *output_fp;
    int  file_open    = 0;
    //    int  is_dir_exits = 0;
    //    struct stat directory;
  
    /*受信タイムアウト用タイマー*/
    struct timeval server_timeout_timer;
    fd_set         fds, readfds;
    double         timer_int, timer_frac;

    /*受信用バッファパラメータ調整用*/
    int socket_buffer_size_image = SOCKET_BUFFER_SIZE_IMAGE*50;
    char sysctl_command[100];

    /*ACK用パラメータ*/
    u_short            protocol_sequence;
    struct in_addr     destACK;
    u_short            portACK;
    char               messACK[ACK_SIZE];
    
    /*受信パケットのIPアドレス記載部分特定*/
    u_short padding_ip=0;

    /* signal for notify port open.*/
     sigset_t  signal_set;
     int       signal_port_open        = SIGNAL_PORT_OPEN;
     /* debug*/
     double time_before_lock, time_after_lock;
     /*NACK制御の場合*/
     u_short  isResend;

     /* 重複パケット(重複orシーケンス抜け!=0, 期待シーケンス==0 */
     int isDepPac;
     
     /*プロセスがデフォルト動作しないようにするシグナルの登録*/
     //port openシグナルを登録
     sigemptyset(&signal_set);
     sigaddset(&signal_set, signal_port_open);
     sigprocmask( SIG_BLOCK, &signal_set, 0);
     
     /*制御ヘッダ長*/
    if(IMAGE_DATA_TRANSFER_WITH_STRAIGHT_UNICAST) {
        padding_ip = 0;
    } else if (IMAGE_DATA_TRANSFER_WITH_HOPBYHOP_ARQ ) {
        padding_ip = ARQ_HEADER_SIZE; //4bytes
    } else if (IMAGE_DATA_TRANSFER_WITH_HOPBYHOP_NACK_ARQ ) {
        padding_ip = NACKBASED_ARQ_HEADER_SIZE; //6bytes
    }

    //受信データログファイル名
    sprintf(filename, "logfile_CISP_recvImageData.log");
 
    //ソケット作成
    memset(&srcAddr, 0, sizeof(srcAddr));
    srcAddr.sin_port        = htons(*port);
    srcAddr.sin_family      = AF_INET;
    srcAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    srcSocket               = socket(AF_INET, SOCK_DGRAM, 0);     /*UDP*/
    if ((status = bind(srcSocket, (struct sockaddr *) &srcAddr, sizeof(srcAddr))) < 0) {
        err(EXIT_FAILURE, "fail to bind (ReceiveImageFile)\n");
    }
    //sending port open signal to the main thread.
    pthread_kill(Main_Thread_ID, signal_port_open);
    
    clientAddrLen           = sizeof(clientAddr);

    //受信用ソケットバッファサイズ拡大
    sprintf   (sysctl_command, "sudo sysctl -w net.core.rmem_max=%d\n",socket_buffer_size_image);
    system    (sysctl_command);
    sprintf   (sysctl_command, "sudo sysctl -w net.core.rmem_default=%d\n",socket_buffer_size_image);
    system    (sysctl_command);
    //setsockopt(srcSocket, SOL_SOCKET, SO_RCVBUF, (char *)&socket_buffer_size_image, sizeof(socket_buffer_size_image));
  

    while (1) {
        /*タイマー値セット*/
        timer_frac                   = modf(3*NumImages*SendingInterval, &timer_int);
        server_timeout_timer.tv_sec  = timer_int;
        server_timeout_timer.tv_usec = 1000000*timer_frac;

        /* 受信タイムアウト設定用 */
        FD_ZERO (&readfds);
        FD_SET  (srcSocket, &readfds);
        memcpy (&fds, &readfds, sizeof(fd_set));
        memset(data_buffer,  0, sizeof(data_buffer));

        /*---ソケットが読みだし可能になるまでwaitする---*/
        if ( select (srcSocket+1, &fds, NULL, NULL, &server_timeout_timer) == 0) {
            /* タイマー値経過までにソケットが読み込み可能にならなければプログラム終了 */
            printf("[timer exceeds... exting.]\n");
            exit(0);
        }
    
        /*データ受信*/
        if(FD_ISSET(srcSocket, &fds)) {
            pacSize = recvfrom(srcSocket
                              ,data_buffer
                              ,SIZE_DATA_BUFFER
                              ,MSG_PEEK,
                              (struct sockaddr *) &clientAddr
                              ,&clientAddrLen);//可変長データなので受信サイズ取得(キューから削除しない)

            numrcv = recvfrom(srcSocket
                              ,data_buffer
                              ,pacSize
                              ,0
                              ,(struct sockaddr *) &clientAddr
                              ,&clientAddrLen);
        }

        
        /*ACK送信 */
        if (IMAGE_DATA_TRANSFER_WITH_HOPBYHOP_ARQ) {
            memcpy  ( &portACK,           data_buffer,                 sizeof(u_short) ); //ポート番号
            memcpy  ( &protocol_sequence, data_buffer+sizeof(u_short), sizeof(u_short) ); //sequence number
            destACK = clientAddr.sin_addr;
            memcpy  (messACK, &protocol_sequence, ACK_SIZE);
            isDepPac = SendingACKorNACK (ACK, destACK, portACK, messACK, ACK_SIZE, FIRST_PAC);  //ACK
        } else if (IMAGE_DATA_TRANSFER_WITH_HOPBYHOP_NACK_ARQ) {
            memcpy  ( &portACK,           data_buffer,                   sizeof(u_short) ); //ポート番号
            memcpy  ( &protocol_sequence, data_buffer+sizeof(u_short),   sizeof(u_short) ); //sequence number
            memcpy  ( &isResend,          data_buffer+2+sizeof(u_short), sizeof(u_short)); //isResend?
            destACK  = clientAddr.sin_addr;
            memcpy  (messACK, &protocol_sequence, ACK_SIZE);
            isDepPac = SendingACKorNACK (NACK, destACK, portACK, messACK, ACK_SIZE, isResend);  //NACK
        }
        /*-受信パケットの処理---*/

        if(isDepPac==0) {
            //送信元IPアドレス記載部分から読み込む
            if (memcpy( &clientIP, data_buffer+padding_ip, sizeof(struct in_addr) ) < 0) {
                printf("cannot memory copy at ReceiveImageFile\n"); 
                exit(EXIT_FAILURE);
            }
            clientIPLen = strlen(inet_ntoa(clientIP));
            memcpy(clientIPstr, inet_ntoa(clientIP), clientIPLen); //文字列変換しスタック領域に保存
            //        printf("[thread %#x receiveIF] %d bytes receievd from %s.\n",pthread_self(),numrcv,clientIPstr);

            //画像データ番号
            if (memcpy( &image_number, data_buffer+padding_ip+sizeof(struct in_addr), sizeof(u_short) ) < 0) {
                printf("cannot memory copy\n"); 
                exit(EXIT_FAILURE);
            }
	
            //パケット番号
            if (memcpy( &sequence, data_buffer+padding_ip+sizeof(struct in_addr)+sizeof(u_short), sizeof(u_short) ) < 0) {
                printf("cannot memory copy\n"); 
                exit(EXIT_FAILURE);
            }
            if (BMP_HANDLER) {
                //BMPヘッダのコピー
                if (memcpy( file_header,
                            data_buffer+padding_ip+sizeof(struct in_addr)+sizeof(u_short)+sizeof(u_short),
                            BMP_HEADER_SIZE ) < 0) {
                    printf("cannot memory copy (BMP header)\n");
                    exit(EXIT_FAILURE);
                } 
            } else if (JPG_HANDLER) {
                //データ部分の先頭2byte格納(JPG先頭記号ffd8判別用)
                if (memcpy( &jpg_first_data, data_buffer+padding_ip+sizeof(struct in_addr)+2*sizeof(u_short), sizeof(u_short) ) < 0) {
                    printf("cannot memory copy (jpg_\n");
                    exit(EXIT_FAILURE);
                } 
                //データの最後尾の2byte格納(JPG終端記号ffd9判別用)
                if (memcpy( &jpg_last_data, data_buffer+numrcv-sizeof(u_short), sizeof(u_short) ) < 0) {
                    printf("cannot memory copy (jpg_last_data)\n");
                    exit(EXIT_FAILURE);
                } 
            }

            //printf("image_number %d, sequence %d\n, raw_data_size %d\n",image_number, sequence, numrcv);
            if (BMP_HANDLER) {
                value_data = numrcv - padding_ip - sizeof(struct in_addr) - 2*sizeof(u_short) - BMP_HEADER_SIZE;
            } else if (JPG_HANDLER) {
                value_data = numrcv - padding_ip - sizeof(struct in_addr) - 2*sizeof(u_short);
            }
	
            /*受信パケット情報の項目作成*/
            if( (data = (struct one_packet_data  *)malloc(sizeof(struct one_packet_data))) == NULL ) {
                err(EXIT_FAILURE, "memory allocation failure. one_packet_data data\n");
            }
            gettimeofday(&current_time, NULL);
            day                   = (int)current_time.tv_sec;
            usec                  = (int)current_time.tv_usec;
            data->day             = day;            //gettimeofdayの受信時刻(整数部分)
            data->usec            = usec;           //上記の小数部分
            data->ip_src          = clientIP;       //送信元アドレス
            data->image_number    = image_number;   //画像シーケンス番号
            data->sequence_number = sequence;       //シーケンス番号先頭
            data->data_size       = value_data;     //受信データサイズ(画像データ部分)

            //        printf("waiting for unloc one_packet_data_list_mutex\n");
            gettimeofday(&current_time, NULL);
            day                   = (int)current_time.tv_sec;
            usec                  = (int)current_time.tv_usec;
            time_before_lock = day + (double)usec/1000000;

        
            /* gettimeofday(&current_time, NULL); */
            /* day                   = (int)current_time.tv_sec; */
            /* usec                  = (int)current_time.tv_usec; */
            /* time_after_lock = day + (double)usec/1000000; */
            printf("[main] waiting for unlock mutex\n");
        
            pthread_mutex_lock(&one_packet_data_list_mutex);   /*--ロック-----*/
            data->next                 = One_Packet_Data_Head.next; //先頭に挿入
            One_Packet_Data_Head.next  = data;                     //受信パケットリストへの連結(先頭方向に)
            pthread_mutex_unlock(&one_packet_data_list_mutex); /*--ロック解除----*/

        

            //--画像ファイル作成(BMP)
            /* メモリ解放していないモジュールの探索用のためコメントアウトしてテスト */
            if(BMP_HANDLER) {
                /* printf("[main] before make image data\n"); */
                   
                makeImageData(clientIP,
                              data->image_number,
                              sequence,
                              file_header,
                              BMP_HEADER_SIZE,
                              data_buffer+padding_ip+sizeof(struct in_addr)+2*sizeof(u_short)+BMP_HEADER_SIZE,
                              value_data,
                              (double)SendingInterval*2.0
                );
            }
   
       
            printf("%d.%06d received volume %d ipaddr %s imagenum %d imgSeq %d protoSec %d (resend? %d)\n"
                   ,day
                   ,usec
                   ,value_data
                   ,inet_ntoa(clientIP)
                   ,image_number
                   ,sequence
                   ,protocol_sequence
                   ,isResend
            );
       
            /* ---受信データ量のログを残す(この部分は重複あり)------ */
            if((fp=fopen(filename,"a")) == NULL) {
                printf("FILE OPEN ERROR %s\n",filename);
                exit (EXIT_FAILURE);
            }

            fprintf(fp,
                    "%d.%06d received volume %d ipaddr %s imagenum %d seq %d\n"
                    ,day
                    ,usec
                    ,value_data
                    ,clientIPstr
                    ,image_number
                    ,sequence);
            fclose(fp);
    
        }
    } /* 重複orシーケンス抜けの場合、受信しない */

  
    close(srcSocket);
}


/*転送データ量通知メッセージ受信部分*/
void ReceiveImageDataValueMessage()
{
    
    /* Paramters listed below are used for esstablishing UDP server.      */
    unsigned short     port     = NOTIFY_DATA_VOLUME_PORT; /* port for recv control message */
    int                srcSocket;        /* UDP server                    */
    struct sockaddr_in srcAddr;
    struct sockaddr_in clientAddr;       /*制御メッセージ送信元アドレス */
    unsigned int       clientAddrLen = sizeof(clientAddr); /* 着信メッセージの長さ       */
    int                status;
    int                numrcv;
    char               *buffer;
    u_short            buffer_size;
    /* regulation parameter  */
    struct in_addr     clientIP;
    u_short            field1, field2; //sequence number [start through  end].
    u_int              field3;         //amount of sent data size from a node.
    
    //現在の末尾のリストのポインタ；
    struct send_data_val_mess *cur, *prev, *mess;
  
    //受信ログファイル作成用
    FILE *fp_sendval;
    char *logfilename = "logfile_CISP_recvValNotifyMess.log";
    struct timeval current_time;
    int            day,usec;
    /*IPアドレス表示部までのパディング*/
    u_short  padding_ip;
    int True = 1;

    /*ACK用パラメータ*/
    u_short            protocol_sequence;
    struct in_addr     destACK;
    u_short            portACK;
    char               messACK[ACK_SIZE];

    //シーケンス重複の検知
    int isSeqDep = 0;
    
    //TCPのパケット受け付け用スレッドのID（特に使わない）
    pthread_t  threadID_makeSendValDataListfromTCPpack;
    struct socketManager *sockMang_new;

    if (NOTIFY_DATA_VOLUME_WITH_HOPBYHOP_TCP) { //TCP による送信
          /*パケット先頭からIPアドレス記載部までのデータ（バイト数）*/
       
        padding_ip  = 0;
        
    
        memset(&srcAddr, 0, sizeof(srcAddr));
        srcAddr.sin_port        = htons(port);
        srcAddr.sin_family      = AF_INET;
        srcAddr.sin_addr.s_addr = htonl(INADDR_ANY);
      
        /* socket */
        srcSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);     /*UDP*/
        setsockopt(srcSocket, SOL_SOCKET, SO_REUSEADDR, &True, sizeof(int)); /*ソケットの再利用*/

        if ((status = bind(srcSocket, (struct sockaddr *) &srcAddr, sizeof(srcAddr))) < 0) {
            err(EXIT_FAILURE, "fail to bind (ReceiveImageDataValueMessage\n");
        }
        //リストヘッドの構造初期化
        Send_Data_Val_Mess_Head.next = &Send_Data_Val_Mess_Head;
        Send_Data_Val_Mess_Head.prev = &Send_Data_Val_Mess_Head;
    
        while(1) {

            /*新たなコネクションはヒープにソケットのための領域を取り、受信受付を行う*/
            sockMang_new = (struct socketManager *) malloc(sizeof(struct socketManager));
                    
            /* 接続の許可 */
            if (listen(srcSocket, 1) != 0) {
                err(EXIT_FAILURE, "fail to listen\n");
            }
        
            if ((sockMang_new->clientSock = accept(srcSocket, (struct sockaddr *) &clientAddr, &clientAddrLen)) < 0) {
                err(EXIT_FAILURE, "fail to accept\n");
            }
            sockMang_new->clientAddr = clientAddr;
            //受信＆リスト化処理スレッド
            if ( pthread_create (&threadID_makeSendValDataListfromTCPpack, NULL, makeSendValDataListfromTCPpack, sockMang_new) < 0) {
                err(EXIT_FAILURE, "error occur\n");
            }
            //ここの部分、１ホップ前のノードからのコネクションしかこないので注意
            printf("Connected from %s create thread %#x(give %p)\n"
                   , inet_ntoa(clientAddr.sin_addr), threadID_makeSendValDataListfromTCPpack, sockMang_new);
            
        } 
           


    } else { //UDP
        /*パケット先頭からIPアドレス記載部までのデータ（バイト数）*/
        if (NOTIFY_DATA_VOLUME_WITH_STRAIGHT_UNICAST) {
            padding_ip  = 0;
            buffer_size = NOTIFY_DATA_VOLUME_SIZE;
        } else if (NOTIFY_DATA_VOLUME_WITH_HOPBYHOP_ARQ) {
            padding_ip  = ARQ_HEADER_SIZE; //シーケンス番号部
            buffer_size = padding_ip + NOTIFY_DATA_VOLUME_SIZE;
        }
        /*バッファ領域確保*/
        if((buffer = (char *)malloc(sizeof(char) * buffer_size)) == NULL) {
            err(EXIT_FAILURE, "fail to allocate memory to the buffer. ReceiveImageDataValueMessage\n");
        }
    
        memset(&srcAddr, 0, sizeof(srcAddr));
        srcAddr.sin_port        = htons(port);
        srcAddr.sin_family      = AF_INET;
        srcAddr.sin_addr.s_addr = htonl(INADDR_ANY);
      
        /* socket */
        srcSocket = socket(AF_INET, SOCK_DGRAM, 0);     /*UDP*/
        setsockopt(srcSocket, SOL_SOCKET, SO_REUSEADDR, &True, sizeof(int)); /*ソケットの再利用*/

        if ((status = bind(srcSocket, (struct sockaddr *) &srcAddr, sizeof(srcAddr))) < 0) {
            err(EXIT_FAILURE, "fail to bind (ReceiveImageDataValueMessage\n");
        }
   
        clientAddrLen = sizeof(clientAddr);

        //リストヘッドの初期化
        Send_Data_Val_Mess_Head.next = &Send_Data_Val_Mess_Head;
        Send_Data_Val_Mess_Head.prev = &Send_Data_Val_Mess_Head;
    
        while(1) {
            //転送データ量通知メッセージ受信
            numrcv = recvfrom(srcSocket, buffer, buffer_size, 0,
                              (struct sockaddr *) &clientAddr, &clientAddrLen);


            /*転送データ量メッセージがhop by hop ARQで転送される場合は、ACKを返す*/
            if(NOTIFY_DATA_VOLUME_WITH_HOPBYHOP_ARQ) {
                memcpy ( &portACK,           buffer,                 sizeof(u_short) ); //ポート番号
                memcpy ( &protocol_sequence, buffer+sizeof(u_short), sizeof(u_short) ); //sequence number
                destACK  = clientAddr.sin_addr;
                memcpy     (messACK, &protocol_sequence, ACK_SIZE);
                isSeqDep = SendingACKorNACK (ACK, destACK, portACK, messACK, ACK_SIZE, FIRST_PAC);  
                /*ACK送信先IPアドレス(子ノード)*/
                //printf("Received %d bytes,sending ACK to ip %s, destport %d \n",numrcv, inet_ntoa(destACK), portACK);
            }

            //シーケンス重複は除いたもののみカウント
            if (isSeqDep == 0) { 
                /* パケットから送信IPアドレスシーケンス番号, データサイズの抜きだし*/
                memcpy( &clientIP, buffer+padding_ip,                                          sizeof(struct in_addr));
                memcpy( &field1,   buffer+padding_ip+sizeof(struct in_addr),                   sizeof(u_short) );
                memcpy( &field2,   buffer+padding_ip+sizeof(struct in_addr)+  sizeof(u_short), sizeof(u_short) );
                memcpy( &field3,   buffer+padding_ip+sizeof(struct in_addr)+2*sizeof(u_short), sizeof(u_int)   );
                printf("[ReceiveImageDataValueMessage %d bytes]  Data val mess is received. ip %s, from %d to %d amnt %d bytes\n"
                       ,numrcv
                       ,inet_ntoa(clientIP)
                       ,field1
                       ,field2
                       ,field3
                );            
                /*リスト挿入準備*/
                mess           = (struct send_data_val_mess  *)malloc(sizeof(struct send_data_val_mess));
                mess->s_start  = field1;
                mess->s_end    = field2;
                mess->ip_src   = clientIP;
                mess->size     = field3;
       
      
                /*send_data_val_messリストアクセス:mutexロック 順方向リストへの挿入*/
                pthread_mutex_lock   (&send_data_val_mess_list_mutex);
                Send_Data_Val_Mess_Head.next->prev = mess;
                mess->next                   = Send_Data_Val_Mess_Head.next;
                mess->prev                   = &Send_Data_Val_Mess_Head;
                Send_Data_Val_Mess_Head.next = mess;
              
                pthread_mutex_unlock (&send_data_val_mess_list_mutex);
                /*mutexアンロック*/

                //ログファイル作成
                if((fp_sendval=fopen(logfilename,"a")) == NULL) {
                    printf("FILE OPEN ERROR %s\n",logfilename);
                    exit (EXIT_FAILURE);
                }
                gettimeofday(&current_time, NULL);
                day   = (int)current_time.tv_sec;
                usec  = (int)current_time.tv_usec;
                fprintf(fp_sendval
                        ,"%d.%06d seq_start %u seq_end %d total %u from %s (isDep?) %d\n"
                        ,day
                        ,usec
                        ,field1
                        ,field2
                        ,field3
                        ,inet_ntoa(clientIP)
                        ,isSeqDep);
                fclose(fp_sendval);
            } /* 重複orシーケンス抜けの場合、受信しない */
        }
    }
    //ここにはこない
    close(srcSocket);
      
}

void *makeSendValDataListfromTCPpack (void *param) {
    /* Paramters listed below are used for esstablishing UDP server.      */
    struct sockaddr_in clientAddr;       /*制御メッセージ送信元アドレス */
    unsigned int       clientAddrLen = sizeof(clientAddr); /* 着信メッセージの長さ       */
    int                status;
    int                numrcv;
    char               *buffer;
    u_short            buffer_size;
    /* regulation parameter  */
    struct in_addr     clientIP;
    u_short            field1, field2; //sequence number [start through  end].
    u_int              field3;         //amount of sent data size from a node.
    
    //現在の末尾のリストのポインタ；
    struct send_data_val_mess *cur, *prev, *mess;
  
    //受信ログファイル作成用
    FILE *fp_sendval;
    char *logfilename = "logfile_CISP_recvValNotifyMess.log";
    struct timeval current_time;
    int            day,usec;
    /*IPアドレス表示部までのパディング*/
    u_short  padding_ip;
    int True = 1;

    /*ACK用パラメータ*/
    u_short            protocol_sequence;
    struct in_addr     destACK;
    u_short            portACK;
    char               messACK[ACK_SIZE];

    //シーケンス重複の検知
    int isSeqDep = 0;
    
    //TCPのパケット受け付け用スレッドのID（特に使わない）
    pthread_t  threadID_recvTCPPacket;
    //型変換
    struct socketManager *sockMang = (struct socketManager *)param;

    buffer_size = NOTIFY_DATA_VOLUME_SIZE;
      
    /*バッファ領域確保*/
    if((buffer = (char *)malloc(sizeof(char) * buffer_size)) == NULL) {
        err(EXIT_FAILURE, "fail to allocate memory to the buffer. ReceiveImageDataValueMessage\n");
    }

    while (1) {
   
 
        //転送データ量通知メッセージ受信
        numrcv = recvfrom(sockMang->clientSock, buffer, buffer_size, 0,
                          (struct sockaddr *) &clientAddr, &clientAddrLen);
        if (numrcv <= 0) {//コネクションロスの場合は-1返すので、ソケットクローズして領域解放 リターン
            close(sockMang->clientSock);
            free(sockMang);
            return;
        }

           
        /* パケットから送信IPアドレスシーケンス番号, データサイズの抜きだし*/
        memcpy( &clientIP, buffer+padding_ip,                                          sizeof(struct in_addr));
        memcpy( &field1,   buffer+padding_ip+sizeof(struct in_addr),                   sizeof(u_short) );
        memcpy( &field2,   buffer+padding_ip+sizeof(struct in_addr)+  sizeof(u_short), sizeof(u_short) );
        memcpy( &field3,   buffer+padding_ip+sizeof(struct in_addr)+2*sizeof(u_short), sizeof(u_int)   );
        printf("[ReceiveImageDataValueMessage %d bytes]  Data val mess is received. ip %s, from %d to %d amnt %d bytes\n"
               ,numrcv
               ,inet_ntoa(clientIP)
               ,field1
               ,field2
               ,field3
        );            
        /*リスト挿入準備*/
        mess           = (struct send_data_val_mess  *)malloc(sizeof(struct send_data_val_mess));
        mess->s_start  = field1;
        mess->s_end    = field2;
        mess->ip_src   = clientIP;
        mess->size     = field3;
       
      
        /*send_data_val_messリストアクセス:mutexロック 順方向リストへの挿入*/
        pthread_mutex_lock   (&send_data_val_mess_list_mutex);
        Send_Data_Val_Mess_Head.next->prev = mess;
        mess->next                   = Send_Data_Val_Mess_Head.next;
        mess->prev                   = &Send_Data_Val_Mess_Head;
        Send_Data_Val_Mess_Head.next = mess;
              
        pthread_mutex_unlock (&send_data_val_mess_list_mutex);
        /*mutexアンロック*/

        //ログファイル作成
        if((fp_sendval=fopen(logfilename,"a")) == NULL) {
            printf("FILE OPEN ERROR %s\n",logfilename);
            exit (EXIT_FAILURE);
        }
        gettimeofday(&current_time, NULL);
        day   = (int)current_time.tv_sec;
        usec  = (int)current_time.tv_usec;
        fprintf(fp_sendval
                ,"%d.%06d seq_start %u seq_end %d total %u from %s (isDep?) %d\n"
                ,day
                ,usec
                ,field1
                ,field2
                ,field3
                ,inet_ntoa(clientIP)
                ,isSeqDep);
        fclose(fp_sendval);
    }
}






/*
 * (受信率算出) タイマハンドラによる呼び出しが行われる
 *              受信率算出に使われた項目は領域解放される
 *
 *              [ただしメッセージが受信されない場合に
 *               そのシーケンス範囲外のパケットの領域が解放されない問題があるので あとで直す]
 */
//[注意]このルーチン終了時、このルーチン内で確保した領域はすべて解放されていることを確認する
void CalcDataRecvRate(int signum)
{
    static int current_cycle     = 0;   //このプログラムはNUM_CYCLE_FOR_EXIT経過後に終了する
    long long int amount_of_recv = 0;
    long long int amount_of_send = 0;
    long long int actual_recv    = 0;
    double        recv_rate      = 0.0;

    /*パケットのポインタ*/
    //現在の末尾のリストのポインタ；
    struct one_packet_data       *pac_cur,   *pac_prev, *pac_proc;
    struct one_image_data        *img_cur,   *img_prev, *img_new;
    u_short                       diff = 0;
    u_short                       last_seq;
    /*送信データ量通知メッセージのポインタ*/
    struct send_data_val_mess    *mess_cur, *mess_prev;
    u_int   list_size = 0;
    
    struct in_addr ip_mess, ip_pac;
    u_short sequence_from, sequence_end, pac_sequence;

    /*受信成功率格納用リストのポインタ*/
    struct recv_data_ratio       *ratio_next, *ratio_prev, *ratio_new;

    //リスト中にヒット:1 なし:0
    int hit_in_ratio = 0;

    //最小受信成功率[<=1.0] (2.0はダミー)
    double min_recv_ratio    = 2.0;
    int overflow=0;
    int del=0;             /*項目削除時1に*/

    /* 受信成功率算出時の時刻 */
    struct timeval current_time;
    int            day,usec;    
    /*受信率のログをとる*/
    char *recvratio_logfile = "logfile_CISP_calcRecvRatio.log";
    FILE *fp;
    int  sequence = 0;
    /*ntpdateコマンド*/
    char ntp_command[50];
    
    /*周期短縮メッセージ送信時1 else 0*/
    u_int    message_interval_makes_short;
    /*周期延長メッセージ送信時1 else 0*/
    u_int    message_interval_makes_long;

    //このブロック、最大150 Mbytesほど必要になる
    /*受信パケット情報リストから画像データ情報リストを作成(この時点では、重複関係なくリストを作成する)*/
    pthread_mutex_lock(&one_packet_data_list_mutex);
    pac_prev = &One_Packet_Data_Head;
    pac_cur  = pac_prev->next;
    pthread_mutex_unlock(&one_packet_data_list_mutex);
    One_Image_Data_Head.list_size_img_data = 0; //リスト長初期化
    One_Image_Data_Head.list_size_pac_data = 0;
    printf("[CalcDataRecvRate]  st 1 before searching recv pac list\n");
    while(pac_cur != NULL ) {

        //オリジナルのデータは、転送データ量メッセージ中の画像データ量以外の場合もあるので、ひとまずコピーする
        if ( (pac_proc = (struct one_packet_data *)malloc (sizeof (struct one_packet_data))) == NULL) {
            err(EXIT_FAILURE, "fail to allocate memory to one_pakcet_data (CalcDataRecvRate)\n");
        }
        //オリジナルデータのコピー
        memcpy (pac_proc, pac_cur, sizeof(struct one_packet_data));

        img_prev = &One_Image_Data_Head;
        img_cur  = img_prev->next;
        while(img_cur != NULL) {
            if ( memcmp(&img_cur->ip_src, &pac_cur->ip_src, sizeof(struct in_addr)) == 0
                 && img_cur->image_number == pac_cur->image_number) {
                break; //img_curのheadに挿入していく
            }
            img_prev = img_cur;
            img_cur  = img_cur->next;
        }

        if(img_cur == NULL ) { //画像データ情報 新規作成。endはこのときの挿入項目となる
            
            if ((img_new = (struct one_image_data *) malloc(sizeof (struct one_image_data))) == NULL ) {
                err(EXIT_FAILURE, "fail to allocate memory to one_image_data data\n");
            }
            img_new->ip_src             = pac_proc->ip_src;
            img_new->image_number       = pac_proc->image_number;
            img_new->image_size         = 0; //このループではまず0にする
            img_prev->next              = img_new;
            img_new->next               = NULL;
            //ヘッドへの項目挿入
            img_new->head.next          = pac_proc;
            pac_proc->next              = NULL;
            //受信パケット&画像データリストサイズの更新
            img_new->list_size_pac_data = 1;
            One_Image_Data_Head.list_size_img_data++;
            /* printf("[CalcDataRecvRate] st 1 making img_cur entry(no.%d) ip %s, number %d\n" */
            /*        ,One_Image_Data_Head.list_size_img_data */
            /*        ,inet_ntoa(pac_proc->ip_src) */
            /*        ,img_new->list_size_pac_data */
            /* ); */
        } else {
            pac_proc->next     = img_cur->head.next; 
            img_cur->head.next = pac_proc;
            img_cur->list_size_pac_data++;
            /* printf("[CalcDataRecvRate] st 1 making img_cur entry ip %s, number %d pac_data size %d\n" */
            /*        ,inet_ntoa(pac_proc->ip_src) */
            /*        ,img_cur->list_size_pac_data */
            /*        ,img_cur->list_size_pac_data */
            /* ); */
        }
        
        
        //end of img_procの処理
        pthread_mutex_lock(&one_packet_data_list_mutex);
        pac_prev = pac_cur;
        pac_cur  = pac_cur->next;
        pthread_mutex_unlock(&one_packet_data_list_mutex);
    }

    /*ヘッドから探索を行い、受信画像データサイズを加算していく。重複したものはカウントしない*/
    /*重複せずに受信したデータログも残しておく(時刻の新しいものが最後に来るように)*/
   
    img_prev = &One_Image_Data_Head;
    img_cur  = img_prev->next;

    while(img_cur != NULL) {
        /* printf("[CalcDataRecvRate] before sort( pac list length %d)\n" */
        /*        ,img_cur->list_size_pac_data */
        /* ); */
        //head以降、要素はシーケンス番号順（小さい方から）にリスト化される
        mergeSort_packetData(&img_cur->head, img_cur->list_size_pac_data );
        pac_prev = &img_cur->head;
        pac_cur  = pac_prev->next;
        diff     = 0;
        last_seq = -1; //sequenceの最小値が0であることを考慮して設定
        while(pac_cur != NULL) {
           
            diff = pac_cur->sequence_number - last_seq;
            
            //受信パケットの中で、重複データを除いて受信データ加算しない
            if (diff >= 1) {
                img_cur->image_size += pac_cur->data_size;

                /*ここで外部ファイルに受信パケット(重複なし)ログを取る*/
                
            }
            //head.next以降、フリーしていく
            last_seq = pac_cur->sequence_number;
            pac_cur  = pac_cur->next; 
            free(pac_prev->next);
            pac_prev->next = pac_cur;
            
        }
        /* printf("[CalcDataRecvRate] st 2  img_cur update ip %s, number %d recv size %d\n" */
        /*        ,inet_ntoa(img_cur->ip_src) */
        /*        ,img_cur->image_number */
        /*        ,img_cur->image_size */
        /* ); */
        //受信データ完成
        img_prev = img_cur;
        img_cur  = img_cur->next;
        
    }
    //end of img_procの処理(この時点でhead.next以降NULLになる)
    mergeSort_imageData(&One_Image_Data_Head,One_Image_Data_Head.list_size_img_data );


    /*転送データ量通知メッセージに対応する受信画像データリストの受信サイズを合計して、リストにする*/

    printf("\n[Calculate image data recved rate from amount of sent image data message.----------]\n");
   
    amount_of_recv               = 0; //送受信データ量
    message_interval_makes_long  = 0; //可変周期設定方式におけるデバッグ用パラメータ
    message_interval_makes_short = 0;

   
    /* printf("[CalcDataRecvRate] st 3 before searching recv mess list\n"); */

    /*リスト操作中 ロック*/
    pthread_mutex_lock(&send_data_val_mess_list_mutex);
    mess_prev = &Send_Data_Val_Mess_Head;
    mess_cur  = Send_Data_Val_Mess_Head.prev;
    pthread_mutex_unlock(&send_data_val_mess_list_mutex);
    
    while(mess_cur != &Send_Data_Val_Mess_Head) { //環状リストなので、探索が終了するとヘッドになる
        /*送信データ量通知メッセージ1つ抽出*/
        ip_mess         = mess_cur->ip_src;
        sequence_from   = mess_cur->s_start;
        sequence_end    = mess_cur->s_end;
        amount_of_send += mess_cur->size;
        //転送データ量通知メッセージの内容をを表示
        printf(  "[  message  ] ipaddr:%s , sec1:%5hu , sec2:%10hu datasize: %10u "
                 ,inet_ntoa(ip_mess) 
                 ,sequence_from
                 ,sequence_end
                 ,mess_cur->size);
	
        /*受信成功率記録用リスト探索*/
        ratio_prev = &Recv_Data_Ratio_Head;
        hit_in_ratio = 0;
        for (ratio_next = Recv_Data_Ratio_Head.next; ratio_next != NULL; ratio_next = ratio_next->next ) {
            if (memcmp (&ratio_next->ip_src, &ip_mess, sizeof (struct in_addr)) == 0) {
                hit_in_ratio = 1;
                break;
            }
            ratio_prev = ratio_next;
        }
        /*項目作成*/
        if (hit_in_ratio==0) {
            if((ratio_new = (struct recv_data_ratio *)malloc(sizeof(struct recv_data_ratio))) == NULL) {
                err(EXIT_FAILURE,"memory allocation failure [ratio_new]\n");
            }
            printf("New ratio calc ipmess %s \n",inet_ntoa(ip_mess));
            ratio_new->ip_src       = ip_mess;
            ratio_new->amount_send  = mess_cur->size;
            ratio_new->amount_recv  = 0;
            ratio_new->next         = NULL;
            ratio_prev->next        = ratio_new;
        }else {
            ratio_next->amount_send += mess_cur->size; //送信データサイズを加算
        }
	
        if ( sequence_from > sequence_end) /*65535から0に戻ったとき overflowフラグ*/
            overflow=1;
      
        /* 
         *     (ip_pac == ip_mess かつ sequence_from <= pac_sequence <= sequence_end )
         *     の条件を満たすパケットサイズを集計する[65525から0にもどった範囲も考慮]]
         *     アクセスした項目は領域解放する
         */
        
        
        actual_recv = 0;

        /*受信画像データリストを探索して、受信データサイズを算出*/
        img_prev = &One_Image_Data_Head;
        /* printf("[CalcDataRecvRate] st 4 before making recv ratio list\n"); */

        for( img_cur = One_Image_Data_Head.next; img_cur!=NULL;  ){
            ip_pac        = img_cur->ip_src;
            pac_sequence  = img_cur->image_number;
	  
            if ( memcmp(&ip_pac, &ip_mess, sizeof(struct in_addr)) == 0 ) {
                if ( overflow==1 ) { /*overflowによってシーケンス番号の範囲を変更*/
                    if ( pac_sequence >= sequence_from || pac_sequence <= sequence_end) {
		
                        /*rintf("element: %s's seq %d is procesing.\n"
                          ,inet_ntoa(img_cur->ip_src)
                          ,img_cur->sequence);*/
		
                        //amount_of_recv += img_cur->size;
                        actual_recv += img_cur->image_size;
                        //		printf("%s count : packet %d\n",inet_ntoa(img_cur->ip_src), img_cur->size);
                        if (hit_in_ratio==0) {
                            ratio_new->amount_recv  += img_cur->image_size;
                        } else {
                            ratio_next->amount_recv += img_cur->image_size;
                        }
                        //del = 1; /*項目削除フラグ*/
                    }
                    
                } else if ( overflow == 0 ) {
                    if ( pac_sequence >= sequence_from && pac_sequence <= sequence_end) {
                        /*printf("element: %s's seq %d is procesing.\n"
                          ,inet_ntoa(img_cur->ip_src)
                          ,img_cur->sequence);*/
		
                        //amount_of_recv += img_cur->size;
                        actual_recv += img_cur->image_size;
                        //		printf("%s count : packet %d\n",inet_ntoa(img_cur->ip_src), img_cur->size);

                        if (hit_in_ratio==0) {
                            ratio_new->amount_recv  += img_cur->image_size;
                        } else {
                            ratio_next->amount_recv += img_cur->image_size;
                        }
                        //del = 1;
                    }
                }
               
            }
            img_cur=img_cur->next;
            img_prev = img_prev->next;
            

        } //end of for

     
        
        /*
         * パケットリストのオリジナルの項目で画像データ番号が
         * sequenceFromからsequenceEndのパケットに該当する項目を削除する
         */
        pthread_mutex_lock(&one_packet_data_list_mutex);
        pac_prev = &One_Packet_Data_Head;
        pac_cur  = One_Packet_Data_Head.next;
        pthread_mutex_unlock(&one_packet_data_list_mutex);

        /* printf("[CalcDataRecvRate] st 5 before calc recv ratio list\n"); */
        del = 0;
        /*受信ビデオパケットリストを探索して処理済みの受信パケットリストを削除*/
        while( pac_cur!=NULL ){
            if ( memcmp(&pac_cur->ip_src, &ip_mess, sizeof(struct in_addr)) == 0 ) {
                if ( overflow==1 ) { /*overflowによってシーケンス番号の範囲を変更*/
                    if(pac_cur->image_number <= sequence_from
                       || pac_cur->image_number <= sequence_end) {
                        del = 1;

                    } 
                }else if (overflow == 0) {
                    if ( pac_cur->image_number >= sequence_from
                         && pac_cur->image_number <= sequence_end) {
                        del = 1;
                    } 
                } 
            }

            if( del == 1) {
                pthread_mutex_lock(&one_packet_data_list_mutex);
                /* printf("[CalcDataRecvRate] orig packet about to free ip %s img num %d seq %d \n" */
                /*        ,inet_ntoa(pac_cur->ip_src) */
                /*        ,pac_cur->image_number */
                /*        ,pac_cur->sequence_number */
                /* ); */
                pac_cur = pac_cur->next;
                free(pac_prev->next);
                pac_prev->next = pac_cur;
                pthread_mutex_unlock(&one_packet_data_list_mutex);
                del = 0;
                
            }else { //削除しない場合、スキップ
                pthread_mutex_lock(&one_packet_data_list_mutex);
                pac_prev = pac_cur;
                pac_cur  = pac_cur->next;
                pthread_mutex_unlock(&one_packet_data_list_mutex);
                                                        
            }
        }
        //デバッグのためのリスト長さ調べ(このエリアは消す)
        pthread_mutex_lock(&one_packet_data_list_mutex);
        pac_prev = &One_Packet_Data_Head;
        pac_cur  = One_Packet_Data_Head.next;
        pthread_mutex_unlock(&one_packet_data_list_mutex);
        list_size = 0;
        /*受信ビデオパケットリストを探索して処理済みの受信パケットリストを削除*/
        while( pac_cur!=NULL ){
           
                pthread_mutex_lock(&one_packet_data_list_mutex);
                pac_prev = pac_cur;
                pac_cur  = pac_cur->next;
                pthread_mutex_unlock(&one_packet_data_list_mutex);
                list_size++;
        
        }
        /* printf("[CalcDataRecvRate] list size is now %d pacs \n",list_size); */

        //上 ここまで
        
        /*そのIPアドレスからの実際の受信データ量*/
        printf(" [ actual ] %10lld\n", actual_recv);
        amount_of_recv += actual_recv;
        overflow=0; 
        
        //転送データ量通知メッセージ、双方向を考慮して領域解放、次の処理へ
        pthread_mutex_lock   (&send_data_val_mess_list_mutex);
        mess_cur = mess_cur->prev;
        mess_cur->next = mess_prev;
        free(mess_prev->prev);
        mess_prev->prev = mess_cur;
        pthread_mutex_unlock (&send_data_val_mess_list_mutex);
        
                
    } //end of 転送データ量通知メッセージ探索、受信成功率算出用データ準備

     
    
    /*画像データ受信成功率算出*/
    if (amount_of_send > 0) { //この[条件式]=[転送データ量通知メッセージが来ている] とする
        //recv_rate  = (double)amount_of_recv/amount_of_send;
        printf("[Recv rate calculation]-----------------------------------------\n");

        if (MIN_CONGESTION_DETECT) {/*検知条件:最小値*/
            /*受信成功率リストからそれぞれ受信成功率を算出、最小値格納*/
            ratio_prev = &Recv_Data_Ratio_Head;      
            for (ratio_next = Recv_Data_Ratio_Head.next; ratio_next != NULL;) {
                if(ratio_next->amount_send>0) {
                    recv_rate  = (double)ratio_next->amount_recv/ratio_next->amount_send;
	  
                    printf("(ipaddr) %s recv_ratio %lf(send %lld recv %lld)\n"
                           ,inet_ntoa(ratio_next->ip_src)
                           ,recv_rate
                           ,ratio_next->amount_send
                           ,ratio_next->amount_recv);
                    if (min_recv_ratio > recv_rate) { //初期値 or 新たに最小値を算出
                        min_recv_ratio = recv_rate;
                    }
                    ratio_prev = ratio_next;
	  
                    ratio_next->amount_recv = ratio_next->amount_send = 0.0;
                }
                /*項目削除*/
                ratio_next = ratio_next->next;
                free(ratio_prev->next);
                ratio_prev->next= ratio_next;
            }
        } else if ( WHOLE_CONGESTION_DETECT) { /*検知条件:全体*/
            min_recv_ratio = (double)amount_of_recv/amount_of_send;
            ratio_prev = &Recv_Data_Ratio_Head;      
            for (ratio_next = Recv_Data_Ratio_Head.next; ratio_next != NULL;) {
                /*項目削除*/
                ratio_next = ratio_next->next;
                free(ratio_prev->next);
                ratio_prev->next= ratio_next;
            }
        }

        if (ADAPTIVE_PERIOD) { /*可変周期設定方式*/
            if ( min_recv_ratio < GAMMA1) {
                SendingInterval = (1+ALPHA)*SendingInterval;
                SendingPeriodControl (SendingInterval);
                message_interval_makes_long  = 1;
                message_interval_makes_short = 0;
	    
            } else if ( min_recv_ratio > GAMMA2) {
                SendingInterval = (1- BETA)*SendingInterval;
                SendingPeriodControl (SendingInterval);
                message_interval_makes_long  = 0;
                message_interval_makes_short = 1;

            }
        }
    }else {
        //        printf("...(any data is not received)\n");
        //デフォルト値2.0なので0にする
        min_recv_ratio = 0.0;
    
    }
    

    /*受信画像データリストを削除*/
    img_prev = &One_Image_Data_Head;
    /* printf("[CalcDataRecvRate] st 4 before making recv ratio list\n"); */
    
    for( img_cur = One_Image_Data_Head.next; img_cur!=NULL;  ){
        img_cur=img_cur->next;
        free(img_prev->next);
        img_prev->next=img_cur;
    }
    
    /*画像データ受信成功率のログ書き込み*/
    if((fp = fopen(recvratio_logfile, "a")) == NULL)  {
        err (EXIT_FAILURE, "cannot open :%s(for saving receiving ratio)\n", recvratio_logfile);
    }
    gettimeofday(&current_time, NULL);
    day   = (int)current_time.tv_sec;
    usec  = (int)current_time.tv_usec;

    /* printf("[CalcDataRecvRate] st 6 before making log\n"); */
            
    printf("[RECV RATIO %.5lf (in message %lld, amount of recv %lld)\n"
           ,min_recv_ratio,amount_of_send,amount_of_recv);
    fprintf(fp,
            "%d.%06d,recv_ratio,%lf,in message,%lld,amount of recv,%lld,sendingPeriod,%lf,congestion,%u,congestion_resolution,%u\n",
            day,
            usec,
            min_recv_ratio,
            amount_of_send,
            amount_of_recv,
            SendingInterval,
            message_interval_makes_long,
            message_interval_makes_short);
    fclose(fp);

    //この関数の呼び出し回数がNUM_CYCLE_FOR_EXIT回経過後、終了コードが送信され、クライアントはすべて終了する
    current_cycle++;
    if ( current_cycle == NUM_CYCLE_FOR_EXIT) {
        printf("[This program exit and EXIT_SUC will broadcast]\n");
        SendingExitCode(1); //終了コードのブロードキャスト
        sleep(20);
        exit(0);
    }


    //[注意]このルーチン終了時、このルーチン内で確保した領域はすべて解放されていることを確認する
}



/* 新周期広告を行う(ブロードキャストバージョンも復活させておく)*/
void SendingPeriodControl (float param) {
    
    unsigned short     servPort;      /* サーバのポート番号                */
    char buf[sizeof(u_short)+sizeof(float)];                      /* u_short 2 bytes + float 4bytes   */
    
    u_short  field1;  //制御のシーケンス番号として利用
    float    field2;  //新周期を格納
    int True = 1;
    /* sending interval*/
    double interval_int, interval_frac;
    //送信ログファイル作成用
    FILE *fp_sendcontrol;
    char *logFileName = "logfile_CISP_sentControlMess.log";
    struct timeval current_time;
    int            day,usec;
    struct sendingPacketInfo sPInfo; //ユニキャスト用
    static u_short sequence_number_vmessage = 1;
    
    // 現在のタイマの解除
    timer_delete(tid);

    interval_frac = modf(NumImages*SendingInterval, &interval_int);
    //printf("interval_frac = %lf, interval_int = %lf\n",interval_frac,interval_int);
    // 新しいタイマ設定
    itval.it_value.tv_sec      = interval_int;  
    itval.it_value.tv_nsec     = interval_frac*1000000000; 
    itval.it_interval.tv_sec   = interval_int;
    itval.it_interval.tv_nsec  = interval_frac*1000000000; 
    
    if(timer_create(CLOCK_REALTIME, NULL, &tid) < 0) { 
        err (EXIT_FAILURE, "timer_create is fail");
    } 
    
    if(timer_settime(tid, 0, &itval, NULL) < 0) { 
        err (EXIT_FAILURE, "timer_settting is fail");
    } 

    servPort     = CONTROL_MESS_PORT; 
    field1       = sequence_number_vmessage++;
    field2       = param;
    
    /*パケット作成*/
    memcpy(buf, &field1, sizeof(u_short)); 
    memcpy(buf+sizeof(u_short), &field2, sizeof(float)); 
    /*パケット送信*/
    //SendingControlMessToChild(CONTROL_MESS_ACK_PORT_MIN, CONTROL_MESS_ACK_PORT_MAX, servPort, buf, sizeof(buf), NULL);
    if(CONTROL_METHOD_UNICAST_WITH_HOPBYHOP_TCP) {
        sPInfo.protocolType = TCP;

    }else if(CONTROL_METHOD_UNICAST_WITH_APPLV_ARQ) {
        sPInfo.protocolType = UDP;
    }
    sPInfo.nextNodeType = CHILDLEN;
    sPInfo.packetType   = NEW_PERIOD_MESS;
    sPInfo.ipAddr       = GetMyIpAddr(WIRELESS_DEVICE); //自ノードのアドレス(ダミー)
    sPInfo.destPort     = CONTROL_MESS_PORT;
    //        sPInfo.dataReceivePort
    sPInfo.ackPortMin   = CONTROL_MESS_ACK_PORT_MIN; //中継用ポートと自ノードからの転送用ポートと分ける
    sPInfo.ackPortMax   = CONTROL_MESS_ACK_PORT_MAX;
    sPInfo.packet       = buf;
    sPInfo.packetSize   = sizeof(buf);
    sPInfo.windowSize   = WINDOW_SIZE;
    sendPacketToNext(sPInfo);
    /*printf("[Sending Control Message] %.3f s to %s sequence %3d\n"
      ,field2
      ,BROADCAST_IP
      ,field1);
    */
    //新周期記録
    if((fp_sendcontrol = fopen(logFileName,"a")) == NULL) {
        err(EXIT_FAILURE, "%s", logFileName);
    }
    gettimeofday(&current_time, NULL);
    day   = (int)current_time.tv_sec;
    usec  = (int)current_time.tv_usec;
    fprintf (fp_sendcontrol
             , "%d.%06d,new_period,%lf,seq,%d\n"
             ,day
             ,usec
             ,field2
             ,field1);

    fclose (fp_sendcontrol);
}


/* プログラム終了コード送信(実験終了コードなので、TCPでなくとも確実に到達すればよい) */
void SendingExitCode (int code) {
    
    int n,  sock;                  /* ソケットディスクリプタ              */
    struct sockaddr_in servAddr;   /* サーバのアドレス                   */
    unsigned short     servPort;   /* サーバのポート番号                 */
    char               *servIP;    /* サーバのIPアドレス                 */
    
    char     buf[sizeof(u_short)];     /* 送信パケット        */
    u_short  exit_code         = 1;    /* 終了コード         */
    int      True              = 1;    /* ブロードキャスト許可*/
    
    struct sendingPacketInfo sPInfo; //ユニキャスト用
    
    servIP       = BEACON_IP;       /*有線インタフェースのブロードキャストアドレス*/
    servPort     = PROGRAM_EXIT_MESS_PORT; /*60002*/

    /*パケット内容*/
    memset(buf, 0, sizeof(buf));        
    memcpy(buf, &exit_code, sizeof(int)); 
    
    /* if(FOR_INDOOR_EXPERIMENT) { /\*(有線インタフェース) 向けにブロードキャスト*\/ */
    /*     if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) */
    /*         err(EXIT_FAILURE, "error while creating sock(SendingExitCode)\n"); */

    /*     memset(&servAddr, 0, sizeof(servAddr));    */
    /*     servAddr.sin_family         = AF_INET;                 */
    /*     servAddr.sin_addr.s_addr    = inet_addr(servIP);  */
    /*     servAddr.sin_port           = htons(servPort);       */
     
    /*     setsockopt(sock,SOL_SOCKET, SO_BROADCAST, (char *)&True, sizeof(True)); */
    /*     n = sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&servAddr, sizeof(servAddr)); */
    /*     close(sock); */
    /* }else { */ /*屋外実験(無線インタフェース向けの送信 ブロードキャスト・ユニキャスト)*/
    sPInfo.nextNodeType = CHILDLEN;
    sPInfo.packetType   = EXIT_CODE;
    sPInfo.protocolType = UDP;
                
    sPInfo.ipAddr       = GetMyIpAddr(WIRELESS_DEVICE); //自ノードのアドレス(ダミー)
    sPInfo.destPort     = PROGRAM_EXIT_MESS_PORT;
    //        sPInfo.dataReceivePort
    sPInfo.ackPortMin   = PROGRAM_EXIT_MESS_ACK_PORT_MIN; //中継用ポートと自ノードからの転送用ポートと分ける
    sPInfo.ackPortMax   = PROGRAM_EXIT_MESS_ACK_PORT_MAX;
    sPInfo.packet       = buf;
    sPInfo.packetSize   = sizeof(buf);
    sPInfo.windowSize   = WINDOW_SIZE;
            
    sendPacketToNext(sPInfo);
    /* } */

    printf("[sending exit code to all client]\n");
    
}
/* 上の関数とほぼ同じ機能(シグナルハンドラとタイマの設定はここでのみ行っている)なので
 * 統合したい。応急処置的に定義した*/
void SendingBeacon (char *dev_for_communication, float param) {
    
    int i,j,n,sw,fs;
    int sock;                      /* ソケットディスクリプタ              */
    struct sockaddr_in servAddr;   /* サーバのアドレス                   */
    unsigned short servPort;       /* サーバのポート番号                 */
    char *servIP;                  /* サーバのIPアドレス                 */
    struct in_addr  myIpAddr;     /* 自身のIPアドレス                    */
    char *string;                  /* サーバへ送信する文字列              */
    char buf[sizeof(struct in_addr)+sizeof(float)];      /* 送信パケット */

    float field1;
    int True = 1;
    int isValid;
    /* sending interval*/
    double interval_int, interval_frac;
    struct sendingPacketInfo sPInfo;
    /*受信率算出タイマ設定 開始*/
    
    memset(&act,    0, sizeof(struct sigaction)); 
    memset(&oldact, 0, sizeof(struct sigaction)); 
    
    // シグナルハンドラ(受信率算出関数 CalcDataRecvRate)の登録 */
    act.sa_handler = CalcDataRecvRate; 
    act.sa_flags   = SA_RESTART; 
    if(sigaction(SIGALRM, &act, &oldact) < 0) { 
        err (EXIT_FAILURE, "sigaction()");
      
    } 
    
    interval_frac = modf(NumImages*SendingInterval, &interval_int);
    
    itval.it_value.tv_sec      = interval_int;  
    itval.it_value.tv_nsec     = interval_frac*1000000000; 
    itval.it_interval.tv_sec   = interval_int;
    itval.it_interval.tv_nsec  = interval_frac*1000000000; 
    printf("[Setting timer for calculating ratio of data value succesary received] %.5lf second.\n"
           ,NumImages*SendingInterval);
    
    if(timer_create(CLOCK_REALTIME, NULL, &tid) < 0) { 
        err (EXIT_FAILURE, "timer_create is fail");
    } 
    
    if(timer_settime(tid, 0, &itval, NULL) < 0) { 
        err (EXIT_FAILURE, "timer_settting is fail");
    } 
    
    /*パケット送信処理*/
    servIP   = BEACON_IP;   /*有線インタフェース利用(INDOOR_EXPERIMENTのみ)*/
    servPort = NOTIFY_INITIAL_PERIOD_PORT; /* 指定のポート番号があれば使用 */

    memset(buf, 0, sizeof(buf));       
    //printf("device(%s): ipv4 addr %s myIpAddr %p\n",dev_for_communication, GetMyIpAddr(dev_for_communication), myIpAddr);

    myIpAddr =  GetMyIpAddr(dev_for_communication);
    memcpy(buf, &myIpAddr, sizeof(struct in_addr)); 
    memcpy(buf+sizeof(struct in_addr), &param, sizeof(float)); 

    
    sPInfo.nextNodeType = CHILDLEN;

    if(CONTROL_METHOD_UNICAST_WITH_HOPBYHOP_TCP) {
        sPInfo.protocolType = TCP;
    } else if (CONTROL_METHOD_UNICAST_WITH_APPLV_ARQ) {
        sPInfo.protocolType = UDP;
    }
    
    sPInfo.packetType   = INITIAL_PERIOD_MESS;
    sPInfo.ipAddr       = GetMyIpAddr(WIRELESS_DEVICE); //自ノードのアドレス(ダミー)
    sPInfo.destPort     = NOTIFY_INITIAL_PERIOD_PORT;
    //        sPInfo.dataReceivePort
    sPInfo.ackPortMin   = NOTIFY_INITIAL_PERIOD_ACK_PORT_MIN; //中継用ポートと自ノードからの転送用ポートと分ける
    sPInfo.ackPortMax   = NOTIFY_INITIAL_PERIOD_ACK_PORT_MAX;
    sPInfo.packet       = buf;
    sPInfo.packetSize   = sizeof(buf);//配列サイズ(スタック領域の)=パケットサイズ
    sPInfo.windowSize   = WINDOW_SIZE;
            
    sendPacketToNext(sPInfo);
        
  
    
}



//merge sort (head->nextから要素が入ってくる
//sort_resultに要素が次々に入る
static void mergeSort_packetData (struct one_packet_data  *head, u_int size)  {

    struct one_packet_data *data_left_head, *data_left, *data_right_head, *data_right_set;
    struct one_packet_data *data_cur,       *data_prev;
    u_int        size_left_set, size_right_set;
    u_short      numl, numr;
   
    
    if ((data_right_head = (struct one_packet_data *)malloc(sizeof(struct one_packet_data))) == NULL) {
        err(EXIT_FAILURE, "cannot allocate memory exit.\n"); //右側だけ必要
    }
    data_right_head->next = NULL;
    
    if( size > 1 ){
        size_left_set  = size / 2;
        size_right_set = size - size_left_set; 

        data_left_head        = head; //head自体に要素はなく、nextから要素である
        data_left             = data_left_head->next;
        /* printf("[mergeSort %#x ] head %p size %d (left %d right %d) \n" */
        /*        ,pthread_self() */
        /*        ,head */
        /*        ,size */
        /*        ,size_left_set */
        /*        ,size_right_set); */
        
        for(numl = 0 ; numl < size_left_set ; data_left = data_left->next, numl++){
            // printf("seq %d\n",data_left->sequenceNumber);
            ;
        }
        data_right_head->next = data_left;
        

        mergeSort_packetData(data_left_head,  size_left_set );
        mergeSort_packetData(data_right_head, size_right_set);
        merge_packetData    (data_left_head, size_left_set, data_right_head, size_right_set, head);
        //途中経過
        data_prev      = head;
        data_cur       = head->next;
        
        /* while (data_cur!=NULL ) { */
        /*     printf ("%d\t",data_cur->sequenceNumber); */
        /*     data_cur = data_cur->next; */
        /* } */
        
        //printf("\n");
        
    }
  
    free(data_right_head); 
}



//merge 
static void merge_packetData (struct one_packet_data  *list1_head, u_int size_left,
                   struct one_packet_data  *list2_head, u_int size_right,
                   struct one_packet_data  *original_head) {

    u_int              numl = 0, numr = 0, numAll = 0, sizeAll;
    struct one_packet_data *data_cur, *data_prev, *data_left_cur,  *data_left_prev, *data_right_cur, *data_right_prev;
    /*    printf("[ merge ] lefthead %p leftsize %d righthead %p rightsize %d originalhead %p\n"
           ,list1_head
           ,size_left
           ,list2_head
           ,size_right
           ,original_head);
    */
    data_prev       = original_head;

    data_left_prev  = list1_head;
    data_left_cur   = data_left_prev->next;
    data_right_prev = list2_head;
    data_right_cur  = data_right_prev->next;
    
    while( numl < size_left || numr < size_right){
        if( numr >= size_right
            || (numl < size_left&& data_left_cur->sequence_number < data_right_cur->sequence_number)){
            data_prev->next = data_left_cur;
            data_left_cur   = data_left_cur->next;
            data_prev       = data_prev->next;
            numl++;
        } else {
            data_prev->next = data_right_cur;
            data_right_cur  = data_right_cur->next;
            data_prev       = data_prev->next;
            numr++;
        }
    }

    data_prev->next = NULL;
   
}



//merge sort (head->nextから要素が入ってくる
//sort_resultに要素が次々に入る
static void mergeSort_imageData (struct one_image_data  *head, u_int size)  {

    struct one_image_data *data_left_head, *data_left, *data_right_head, *data_right_set;
    struct one_image_data *data_cur,       *data_prev;
    u_int        size_left_set, size_right_set;
    u_short      numl, numr;
   
    
    if ((data_right_head = (struct one_image_data *)malloc(sizeof(struct one_image_data))) == NULL) {
        err(EXIT_FAILURE, "cannot allocate memory exit.\n"); //右側だけ必要
    }
    data_right_head->next = NULL;
    
    if( size > 1 ){
        size_left_set  = size / 2;
        size_right_set = size - size_left_set; 

        data_left_head        = head; //head自体に要素はなく、nextから要素である
        data_left             = data_left_head->next;
        /*printf("[mergeSort %#x ] head %p size %d (left %d right %d) \n"
               ,pthread_self()
               ,head
               ,size
               ,size_left_set
               ,size_right_set);*/
        
        for(numl = 0 ; numl < size_left_set ; data_left = data_left->next, numl++){
            // printf("seq %d\n",data_left->sequenceNumber);
            ;
        }
        data_right_head->next = data_left;
        

        mergeSort_imageData(data_left_head,  size_left_set );
        mergeSort_imageData(data_right_head, size_right_set);
        merge_imageData    (data_left_head, size_left_set, data_right_head, size_right_set, head);
        //途中経過
        data_prev      = head;
        data_cur       = head->next;
        
        /* while (data_cur!=NULL ) { */
        /*     printf ("%d\t",data_cur->sequenceNumber); */
        /*     data_cur = data_cur->next; */
        /* } */
        
        //printf("\n");
        
    }
  
    free(data_right_head); 
}



//merge 
static void merge_imageData (struct one_image_data  *list1_head, u_int size_left,
                   struct one_image_data  *list2_head, u_int size_right,
                   struct one_image_data  *original_head) {

    u_int              numl = 0, numr = 0, numAll = 0, sizeAll;
    struct one_image_data *data_cur, *data_prev, *data_left_cur,  *data_left_prev, *data_right_cur, *data_right_prev;
    /*    printf("[ merge ] lefthead %p leftsize %d righthead %p rightsize %d originalhead %p\n"
           ,list1_head
           ,size_left
           ,list2_head
           ,size_right
           ,original_head);
    */
    data_prev       = original_head;

    data_left_prev  = list1_head;
    data_left_cur   = data_left_prev->next;
    data_right_prev = list2_head;
    data_right_cur  = data_right_prev->next;
    
    while( numl < size_left || numr < size_right){
        if( numr >= size_right
            || (numl < size_left&& data_left_cur->image_number < data_right_cur->image_number)){
            data_prev->next = data_left_cur;
            data_left_cur   = data_left_cur->next;
            data_prev       = data_prev->next;
            numl++;
        } else {
            data_prev->next = data_right_cur;
            data_right_cur  = data_right_cur->next;
            data_prev       = data_prev->next;
            numr++;
        }
    }

    data_prev->next = NULL;
   
}
