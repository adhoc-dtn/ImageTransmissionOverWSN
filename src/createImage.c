#include "common.h"

/*
 * 受信データをディスクに書き込み、指定時間経過後にシーケンス番号順にソートして、再保存する
 * 重複されて受信したデータは最保存時に削除する。受信できず抜けた部分は0xffで画像データをパディングする
 *
 * [注意]  システム稼働時に、[各ソースIPアドレス]ディレクトリ以下の画像ファイルはすべて削除しておくこと
 *         ディレクトリごと削除してもOK  画像ファイルが残っている場合は、エラーして終了する
 * 


 * 受信データは BMP windows V3を想定。この形式でテストする。JPGでもできるようにしたい
 * JPGでは、ソート済みのファイルの先頭を0xffd8, 終端を0xffd9にする必要がある
 *
 * (一時ファイルの名前) ./[送信元IPアドレス名]/t
 *  ____________________________________________________________
 * |  画像データのヘッダ  | データ部分（１パケット） |   . . .    
 * |   ( 54 byte)        |   ( XXXX byte 不定)    |  (受信順にデータ格納)
 *  ------------------------------------------------------------
 *
 * シーケンス番号順にソートすると、以下の用にファイルが完成する
 *
 *
 *
 */


//各パケットの情報リスト（１画像データ分をリスト状に連結）
struct image_data {
    u_short               sequenceNumber;        //シーケンスナンバ
    u_int                 dataIndex;             //保存用一時ファイル内のインデックス(ヘッダを除いた先頭からのバイト数)
    u_short               dataSize;              //１パケットデータサイズ（シーケンスナンバ等を除いた画像データ部分のみの）
    pthread_mutex_t      *image_data_mutex;     //排他制御用mutex 対応するimage_of_node構造体mutexのアドレスを使う
    struct image_data    *next;
};

//送信元IPアドレス＆画像番号の情報リスト dataHead.nextに各パケット（上述）が連結される
struct image_of_node {
    char                 *tempFileName;    //画像の一時保存先ファイル名
    struct in_addr        ipAddr;           //送信元IPアドレス
    u_int                 listSizePacket;   //パケット情報リストの合計リストサイズ(受信パケット数に対応)
    u_int                 listSizeBytes;    //パケット情報リストの合計受信パケットサイズ（ヘッダを除いた）
    u_short               imageNumber;      //画像番号
    u_short               headerSize;       //画像データのヘッダ(BMP windows V3なら54 バイト)
    struct image_data     dataHead;         //受信データのヘッド部分（ここには要素を入れず、next以降に順次挿入する
    struct image_data     *dataEnd;          //受信データの末端部分(nextがNULLとなる)
    pthread_mutex_t       image_data_mutex; //排他制御用mutex(実体)
    pthread_t             threadID_makeOneImageFileOneAddr; //画像データ作成スレッドのID
    u_short               threadStatus;     //画像データ作成スレッドの状態(EXIT 終了, START 起動中)
    double                timerCreateImage; //画像作成タイマ
    pthread_t             threadID_delItemFromImageOfNode; //項目削除用スレッドID
    struct image_of_node *next;
};

struct image_of_node      ImageNodeHead;          //アドレス、画像番号情報リストのヘッド部分(nextから要素を入れる)
pthread_mutex_t           ImageOfNodeMutex = PTHREAD_MUTEX_INITIALIZER; //上記リスト全体のmutex

/*ipAddrディレクトリ以下に画像データを作成する*/
void makeImageData(struct in_addr ipAddr,
                   u_short imageNumber,
                   u_short sequenceNumber,
                   char    header[],
                   u_short headerSize,
                   char    data[],
                   u_short dataSize,
                   double  timerCreateImage);

/*特定のIPアドレスからの画像ファイルを送信順にソートして作成する*/
static void *makeOneImageFileOneAddr(void *dataInfo);
/*不要なリスト要素を削除する*/
static void *delItemFromImageOfNode (void *param);

static pthread_t threadID_delItemFromImageOfNode     = 0;           //リスト項目削除用スレッド
static u_short   threadStatus_delItemFromImageOfNode = THREAD_EXIT; //初期状態

/*リスト要素のマージソートを行いリストの再構築*/
static void mergeSort  (struct image_data  *head,       u_int size);
static void merge      (struct image_data  *list1_head, u_int size_left,
                        struct image_data  *list2_head, u_int size_right,
                        struct image_data  *original_head);

/* /\*jpg先頭記号(ffd8, ffe0)挿入*\/ */
/* static void Insert_FFD8_FFE0    (char   *tmp_filename); */
/* /\*BMPヘッダを挿入*\/ */
/* static void InsertHeader        (char   *tmp_filename, char *header, u_short size); */
/* /\* シーケンスの抜けの分,paddingする*\/ */
/* static void PaddingZero         (char   *tmp_filename, u_int num_of_padding); */
/* /\*画像作成*\/ */
/* static void CreateImage         (char   *clientIP,   */
/*                                  struct image_of_node *processing_data,  */
/*                                  char   *tmp_filename); */

#define  TEMP_FILE_NAME   "tmpfile"

/* /\* JPG画像作成に関わる定数 *\/ */
/* #define JPG_FIRST             (0xffd8) */
/* #define JPG_TYPE_ZERO         (0xffe0) */
/* #define JPG_LAST              (0xffd9) */


/*makeImageDataは、mutex_lockでブロックされる恐れがある。（ほぼ考えられないが）
 *安全な方法は、受信データはキューの中に入れ、一時ファイル作成[スレッド]にシグナルを送る。
 * シグナルを受信したら、デキューして処理を開始する、というふうになおしたほうがよい?*/
/*でもエンキュー時にキューアクセスのロックが必要なので、同じ結果となるのかもしれない*/



/**
 * @param ipAddr           データ送信元IPアドレス
 *        imageNumber      画像データ番号
 *        sequenceNumber   パケット番号
 *        header           画像データヘッダ（なければNULL）
 *        headerSize       ヘッダサイズ
 *        data             画像データ（１パケット分）
 *        dataSize         画像データサイズ（１パケット分の）
 *        timerCreateImage 
 **/
void makeImageData(struct in_addr ipAddr,
                   u_short imageNumber,
                   u_short sequenceNumber,
                   char    header[],
                   u_short headerSize,
                   char    *data,
                   u_short dataSize,
                   double  timerCreateImage)
{
    
    int    is_dir_exits;    //ディレクトリ存在確認
    struct stat directory;  //ディレクトリ情報(inodeの)
    char   sourceIPstr[20];
    //アドレス画像番号情報リスト探索用ポインタ（現在、１つ前）新規挿入用 操作対象
    struct image_of_node  *IoN_cur, *IoN_prev, *IoN_new, *IoN_proc;
    struct image_data     *ID_cur,  *ID_prev,  *ID_new;  //各パケットの情報リスト探索用ポインタ（現在、１つ前）新規挿入用
    FILE   *tmpfile_fp; //一時ファイル用ファイルポインタ
    char   tmp_filename[50];
    
    /*項目削除スレッドをアクティブ*/
    if(threadStatus_delItemFromImageOfNode == THREAD_EXIT) {
        threadStatus_delItemFromImageOfNode = THREAD_START;
        if (pthread_create (&threadID_delItemFromImageOfNode, NULL, delItemFromImageOfNode, &ImageNodeHead) != 0) {
            err(EXIT_FAILURE, "fail to create delItemFromImageOfNode\n");
        }
    }
    //IPアドレスを文字列化
    strcpy (sourceIPstr, inet_ntoa(ipAddr));
    /*------------ 画像ファイル作成するか否かという処理 ---------------*/
    /* ディレクトリ存在確認 なければ作る */
    if ((is_dir_exits = stat(sourceIPstr, &directory)) != 0 ) { 
        if ( mkdir( sourceIPstr,
                    S_IRUSR | S_IWUSR | S_IXUSR |         /* rwx */
                    S_IRGRP | S_IWGRP | S_IXGRP |         /* rwx */
                    S_IROTH | S_IXOTH | S_IXOTH) != 0) {  /* rwx */
            err(EXIT_FAILURE, "dir %s is not created\n", sourceIPstr);
        }
    }
    /*
     * 受信データ書き込み用一時ファイル名(保存ディレクトリ名は送信元IPアドレスに対応、tmpファイルは画像番号に対応する
     * (異なる画像データ番号のファイルが同時に作成されることを考慮)
     */
    sprintf(tmp_filename, "%s/%s%03d",sourceIPstr,TEMP_FILE_NAME,imageNumber);
    
    /* printf("[makeImageData] before lock ImageOfNodeMutex\n"); */
    /*!- lock -!*/
    pthread_mutex_lock(&ImageOfNodeMutex);
    /*アドレス、画像番号情報リストから該当IPアドレス,画像データ番号の項目を探索*/
    IoN_prev = &ImageNodeHead;
    for( IoN_cur = ImageNodeHead.next; IoN_cur != NULL; IoN_cur = IoN_cur->next) {
        if ( memcmp(&IoN_cur->ipAddr, &ipAddr, sizeof (struct in_addr)) == 0
             && IoN_cur->imageNumber == imageNumber)  
            break;
      
        IoN_prev = IoN_cur;
    }
  

    
    /*---リストの探索完了---------------------*/
    if(IoN_cur == NULL ) {  //該当IP・画像番号データからの最初の受信  新規項目作成
         /* printf("[makeImageData] new item is created: ip %s, imageNumber %d seq %d\n"  */
         /*        ,inet_ntoa(ipAddr)  */
         /*        ,imageNumber  */
         /*        ,sequenceNumber  */
         /* );  */
        
        
        if( (IoN_new               = (struct image_of_node *)malloc (sizeof( struct image_of_node))) == NULL ) {
            err(EXIT_FAILURE, "fail to allocate memory for new item\n");
        }
        if( (IoN_new->tempFileName = (char *)malloc(sizeof(char)*(strlen(tmp_filename)+1))) == NULL ) {
            err(EXIT_FAILURE, "fail to allocate memory for temofilename\n"); //\0含めて領域作る
        }
        strcpy(IoN_new->tempFileName, tmp_filename);//一時ファイル名
        IoN_new->ipAddr           = ipAddr;
        IoN_new->listSizePacket   = 0;
        IoN_new->listSizeBytes    = 0;
        IoN_new->imageNumber      = imageNumber;
        IoN_new->headerSize       = headerSize;
        IoN_new->image_data_mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
        IoN_new->threadStatus     = THREAD_START;
        IoN_new->timerCreateImage = timerCreateImage;
        IoN_new->dataHead.next    = NULL; //データヘッドにNULL初期化
        IoN_new->dataEnd          = &IoN_new->dataHead; //末端部分 nextに挿入していく
        IoN_prev->next            = IoN_new;
        IoN_new->next             = NULL; //情報リスト、next
      
        if (BMP_HANDLER) {
            //InsertHeader   (tmp_filename, header, headerSize); //一時ファイルにBMPヘッダ挿入
            if((tmpfile_fp = fopen(tmp_filename, "w")) == NULL) 
                err (EXIT_FAILURE, "cannot open :%s(for saving temporary image data.)\n", tmp_filename);
            
            if (fwrite (header, sizeof(char),  headerSize,  tmpfile_fp) < 0 )  //ヘッダの書き込み
                err (EXIT_FAILURE, "cannot write header: %s\n",tmp_filename);
            
            fclose(tmpfile_fp);
        }
        
        /*タイマーをつけてデータソート&画像作成スレッドcreateする*/
        if(pthread_create(&IoN_new->threadID_makeOneImageFileOneAddr, NULL, &makeOneImageFileOneAddr, IoN_new) != 0) {
            err(EXIT_FAILURE, "fail to create makeOneImageFileOneAddr\n");
        }
        IoN_proc = IoN_new;
    } else { //探索成功した場合
        /*  printf("[makeImageData]  received and add to item: ip %s, imageNumber %d seq %d\n" */
        /*        ,inet_ntoa(ipAddr) */
        /*        ,imageNumber */
        /*        ,sequenceNumber */
        /* ); */
         IoN_proc = IoN_cur;
    }
    pthread_mutex_unlock(&ImageOfNodeMutex);
    /* printf("[makeImageData] after unlock ImageOfNodeMutex\n"); */

    /* printf("[makeImageData] before lock IoN_proc->image_data_mutex\n"); */
 
    pthread_mutex_lock(&IoN_proc->image_data_mutex);
    //一時ファイルへの受信データの書き込み（この区間も、画像createのタイミングと被る場合があるのでmutex_lockかけたまま）
    if((tmpfile_fp = fopen(tmp_filename, "a")) == NULL)  {
        err (EXIT_FAILURE, "L187 cannot open :%s(for tempolary saving payload image data)\n", tmp_filename);
    }
    //    printf(" file open \n");

    if (fwrite (data, sizeof(char) ,dataSize ,tmpfile_fp) < 0 ) { 
        err (EXIT_FAILURE, "cannot write file: %s\n",tmp_filename);
    }
    //    printf("data added to tempfile \n");
    fclose (tmpfile_fp);
    
    //リスト末端に新要素を挿入
    
    /* ID_prev = &IoN_proc->dataHead; */
    /* for( ID_cur = IoN_proc->dataHead.next; ID_cur != NULL; ID_prev = ID_cur, ID_cur = ID_cur->next)  */
    /*     ; //末端まで走査 */

    //パケットデータ挿入
    if ((ID_new    = (struct image_data *)malloc(sizeof(struct image_data))) == NULL ) {
        err(EXIT_FAILURE, "fail to allocate memory for item (image_data)\n");
    }
    ID_new->sequenceNumber   = sequenceNumber;
    ID_new->dataIndex        = IoN_proc->listSizeBytes;
    ID_new->dataSize         = dataSize;
    ID_new->image_data_mutex = &IoN_proc->image_data_mutex;//実体の参照
    ID_new->next             = NULL;
    IoN_proc->dataEnd->next  = ID_new;  //末端に挿入
    IoN_proc->dataEnd        = ID_new;  //新たな末端への参照
    IoN_proc->listSizePacket++;         //リストサイズ（パケット数、更新）
    IoN_proc->listSizeBytes+=dataSize;  //リストサイズ（受信データ・バイト）更新
    /* printf("[makeImageData]  add to item: ip %s, imageNumber %d seq %d ( now listSizePacket %d,listSizeBytes %d)\n" */
    /*        ,inet_ntoa(ipAddr) */
    /*        ,imageNumber */
    /*        ,sequenceNumber */
    /*        ,IoN_proc->listSizePacket */
    /*        ,IoN_proc->listSizeBytes */
    /* ); */
    pthread_mutex_unlock(&IoN_proc->image_data_mutex);
    /* printf("[makeImageData] after unlock IoN_proc->image_data_mutex\n"); */

    
   
      /*!- unlock -!*/


}


/* 特定のIPアドレスからの画像ファイルを送信順にソートして作成する */
/** @param  param 送信元IPアドレス＆画像番号の情報リスト項目  (struct image_of_data)
 *
 *
 */
static void *makeOneImageFileOneAddr(void *param) {

    struct image_of_node  *IoN_proc; //操作対象
    struct image_data     *ID_cur,  *ID_prev,  *ID_new;  //各パケットの情報リスト探索用ポインタ（現在、１つ前）新規挿入用

   
    /* for sending interval*/
    double timer_raw;
    double interval_int, interval_frac;
    char   image_filename[100];                 /*  ソート済み画像ファイル名                    */
    char   mv_command    [200];                /*  TEMPファイル名前変更用コマンド             */
    char   rm_tempfile   [200];                 /*  TEMPファイル削除用コマンド                */
    char   format        [5];                   /*  データフォーマット                        */
    int    in_tmpfile_d;                        /* 一時ファイル読み込み用ファイルディスクリプタ */
    FILE     *in_tmpfile, *in_imageFile;          /*  作成画像ファイルのファイルポインタ          */
    char   *fileHeader;                         /*  一時ファイル中のヘッダデータ                */
    struct stat state_of_file;                  /* 画像ファイルのステータス格納用               */
    long   fileOffset;                         /* tmpファイルからのオフセット(バイト)           */
    int diffSeqNum;                   /* シーケンス同士の差分 0==重複シーケンス、1==抜けなしの配送
                                                 * >1シーケンス抜けあり                           */
    u_int  numPaddingMax;               
    u_int  numPadding;
    u_char white = 255;                         /*白色のパディング*/
    char   dataBuffer[SIZE_DATA_BUFFER];         /*一時ファイルからのデータ読み込み用バッファ*/
    /*ブロックするシグナルの登録*/
    sigset_t              signal_set;
    int    signal_del_item_image = SIGNAL_DEL_ITEM_IMAGE;
    
    
    sigemptyset     ( &signal_set );
    sigaddset       ( &signal_set, signal_del_item_image);
    pthread_sigmask ( SIG_BLOCK, &signal_set, 0 );
    /*デタッチ状態*/
    if(pthread_detach(pthread_self()) != 0) {
        err(EXIT_FAILURE, "thread detach is faild (sendWaitAndResend)\n");
    }

    /*引数型変換*/
    IoN_proc      = (struct image_of_node *)param;
    timer_raw     = IoN_proc->timerCreateImage;    //タイマー
    interval_frac = modf(timer_raw, &interval_int); //スリープ用に分割

    
    if(JPG_HANDLER) {
        sprintf(format, "jpg");
    } else if (BMP_HANDLER) {
        sprintf(format, "bmp");
    }

    
    /*作成画像ファイル名*/
    if(SAVE_PICTURES) {
        sprintf (image_filename, 
                 "%s/Image_number%03u.%s"
                 ,inet_ntoa(IoN_proc->ipAddr)
                 ,IoN_proc->imageNumber
                 ,format);
    } else {
        sprintf (image_filename, 
                 "%s/most_resent.%s"
                 ,inet_ntoa(IoN_proc->ipAddr)
                 ,format);
    }        
    
    /*タイマー与えて起動*/
     sleep((unsigned int)interval_int);
     usleep (interval_frac*1000000);
     printf("[makeOneImageFileOneAddr %#x] start to %s  from %s (list length %d all bytes %d) \n"
            ,pthread_self()
            ,image_filename
            ,IoN_proc->tempFileName
            ,IoN_proc->listSizePacket
            ,IoN_proc->listSizeBytes
     );
     
     /*受信パケットをソートして、画像ファイル作成、tempファイル削除*/
     /*シーケンス番号順にソート　ID_prev->sequenceNumber <= ID_cur->sequenceNumberとなる*/

     pthread_mutex_lock(&IoN_proc->image_data_mutex);
     mergeSort(&IoN_proc->dataHead, IoN_proc->listSizePacket);
     pthread_mutex_unlock(&IoN_proc->image_data_mutex);
     
     //画像ファイルがすでに作成してある場合、今回作成分は大きな遅延を伴って受信されたデータとして処理し、
     //画像ファイルを作成しない
     if (stat  (image_filename, &state_of_file) != 0) {
         //書き込み用画像ファイル
         if ((in_imageFile = fopen(image_filename, "a")) < 0)
             err(EXIT_FAILURE, "read erro at file %s. ", image_filename);

         /*対応する一時ファイルからヘッダを取り出し、画像データに保存*/
         if(BMP_HANDLER) {
             //一時ファイルオープン
             if ((in_tmpfile_d   = open(IoN_proc->tempFileName, O_RDONLY)) < 0)
                 err(EXIT_FAILURE, "read erro at file %s. ", IoN_proc->tempFileName);
             /*一時データからヘッダ読み込み*/
             if ((fileHeader = (char *)malloc(sizeof(char)*IoN_proc->headerSize) ) == NULL ) {
                 err(EXIT_FAILURE, "fail to allocate memory\n");
             }
             //初期化 
             memset (fileHeader, 0, IoN_proc->headerSize);
             if( read(in_tmpfile_d, fileHeader, IoN_proc->headerSize) < 0) { 
                 err(EXIT_FAILURE, "an error occurs while reading bmp file header\n");
             }
             close(in_tmpfile_d);

             //ヘッダの書き込み
             if (fwrite (fileHeader, sizeof(char), IoN_proc->headerSize , in_imageFile ) < 0 )  //ヘッダの書き込み
                 err (EXIT_FAILURE, "cannot write header: %s\n",image_filename);
             free(fileHeader);
         }

         //一時ファイルオープン、インデックスから受信パケットサイズ分読み込み、画像データに書き込み
     
         if ((in_tmpfile = fopen(IoN_proc->tempFileName, "rb")) < 0)
             err(EXIT_FAILURE, "read erro at file %s. ",IoN_proc->tempFileName );

         
         /*インデックスからファイルを１パケット分だけ読み込み、書き出しを行う。*/
         ID_prev = &IoN_proc->dataHead;
         for (ID_cur = IoN_proc->dataHead.next; ID_cur != NULL;) {

             fileOffset = IoN_proc->headerSize + ID_cur->dataIndex; //一時ファイル中で読み込みに該当するオフセット

             //オーバーフローして最大値から0に戻る場合を香料
             if (ID_prev == &IoN_proc->dataHead) { //ヘッドの場合
                 diffSeqNum = ID_cur->sequenceNumber; 
                 /* printf("[makeOneImageFileOneAddr] diff %d (curseq %d)\n" */
                 /*        ,diffSeqNum */
                 /*        ,ID_cur->sequenceNumber); */

             } else {
                 if ((diffSeqNum = ID_cur->sequenceNumber - ID_prev->sequenceNumber) < 0) {
                     diffSeqNum+=UINT_MAX;
                 }
                 
                   /* printf("[makeOneImageFileOneAddr] diff %d (lastseq %d, curseq %d)\n" */
                   /*      ,diffSeqNum */
                   /*      ,ID_prev->sequenceNumber */
                   /*      ,ID_cur->sequenceNumber); */
             }
           
             if ( ID_prev == &IoN_proc->dataHead || diffSeqNum >= 1) { //seq == 0（最初）以降は重複分を書き換えない
                 
                 if (diffSeqNum > 1) { //抜けを検知 抜け分パディングする
                     if (ID_prev == &IoN_proc->dataHead) {
                         numPaddingMax = diffSeqNum  * ID_cur->dataSize;

                     }else {
                         numPaddingMax = (diffSeqNum - 1 ) * ID_cur->dataSize;
                     }
                     /* printf("[makeOneImageFileOneAddr] padding %lld X %d bytes ( %lld bytes)\n" */
                     /*        ,(diffSeqNum - 1 ) */
                     /*        , ID_cur->dataSize */
                     /*        ,numPaddingMax); */
                     
                     for ( numPadding = 0; numPadding < numPaddingMax; numPadding++) {
                         fputc(white, in_imageFile);
                     }
                 }
                 fseek  (in_tmpfile, fileOffset,       SEEK_SET); //インデックスをシーク
                 fread  (dataBuffer, sizeof(char), ID_cur->dataSize, in_tmpfile);
                 fwrite (dataBuffer, sizeof(char), ID_cur->dataSize, in_imageFile );
                 /* printf("[makeOneImageFileOneAddr] writing seq %d, size %d B (index %d bytes)\n" */
                 /*        , ID_cur->sequenceNumber */
                 /*        , ID_cur->dataSize */
                 /*        ,fileOffset */
                 /* ); */
                 
             } /*同じシーケンス番号のデータの場合、何もしない*/
             ID_prev = ID_cur;
             ID_cur  = ID_cur->next;
         }

         //ファイルクローズ
         fclose(in_tmpfile);
         fclose(in_imageFile);
         /* printf("new image file %s is successfully created.\n",image_filename); */
     }
     
     /*一時ファイルの削除*/
     /*一時ファイル削除用コマンド*/
    /* sprintf(rm_tempfile, */
    /*         "rm %s\n",IoN_proc->tempFileName); */
    /* system(rm_tempfile); */

    IoN_proc->threadStatus = THREAD_EXIT;
    /*スレッドexitし、項目削除シグナルを送信*/
    if(pthread_kill(threadID_delItemFromImageOfNode, signal_del_item_image) != 0) {
        err(EXIT_FAILURE, "fail to send signal to item deleting thread.\n");
    }
    
    pthread_exit(NULL);
   
}

/*送信元IPアドレス、画像番号情報リストの項目削除*/
static void *delItemFromImageOfNode (void *param) {

    /*型変換用*/
    struct image_of_node *IoN_head;   //引数の型変換用、リストヘッド
    struct image_of_node *IoN_cur, *IoN_prev; //項目削除用 現在、１つ前のポインタ
    struct image_data    *ID_cur,  *ID_prev;   //同じ

    //削除用シグナルを受信したら、リストの要素削除を開始する。
    sigset_t  signal_set;
    int       signal_del_item_image = SIGNAL_DEL_ITEM_IMAGE;
  
    sigemptyset     ( &signal_set );
    sigaddset       ( &signal_set, signal_del_item_image );   // dequeue
    pthread_sigmask ( SIG_BLOCK, &signal_set, 0 );

    /*型変換*/
    IoN_head = (struct image_of_node *)param;
    
    while(1) {
        //キュー要素削除シグナル受信までwait
        sigwait(&signal_set, &signal_del_item_image);
        /*!-lock-!*/
        printf("(delItemFromImageOfNode %#x) received del item signal start to delete item\n",pthread_self());

        pthread_mutex_lock(&ImageOfNodeMutex); //送信元IPアドレス＆画像番号の情報リスト アクセスのロック
        IoN_prev = IoN_head;
        IoN_cur = IoN_head->next;
        pthread_mutex_unlock(&ImageOfNodeMutex);

        while( IoN_cur != NULL ) { 

            if(IoN_cur->threadStatus == THREAD_EXIT) { //スレッドの終了→項目削除開始
                printf("[delItemFromImageOfNode ID %#x] Delete ip %s imageNumber %d\n"
                       ,pthread_self()
                       ,inet_ntoa(IoN_cur->ipAddr)
                       ,IoN_cur->imageNumber);

                pthread_mutex_lock(&IoN_cur->image_data_mutex); //送信元IPアドレス＆画像番号の情報リスト アクセスのロック
                ID_prev = &IoN_cur->dataHead;
                ID_cur = IoN_cur->dataHead.next;
                pthread_mutex_unlock(&IoN_cur->image_data_mutex);

                //ここ、工事中
                while ( ID_cur != NULL ) {
                    //送信元IPアドレス＆画像番号の情報リスト アクセスのロック
                    pthread_mutex_lock(&IoN_cur->image_data_mutex); 
                    ID_cur = ID_cur->next;    
                    free(ID_prev->next);           //他の領域を解放
                    ID_prev->next  = ID_cur;
                    pthread_mutex_unlock(&IoN_cur->image_data_mutex);

                }

                //pthread_mutex_unlock(&IoN_cur->image_data_mutex);
                
                pthread_mutex_lock(&ImageOfNodeMutex); //送信元IPアドレス＆画像番号の情報リスト アクセスのロック
                free(IoN_cur->tempFileName);    //まずパケットの領域を解放
                IoN_cur = IoN_cur->next;    
                free(IoN_prev->next);           //他の領域を解放
                IoN_prev->next  = IoN_cur;
                pthread_mutex_unlock(&ImageOfNodeMutex);

            } else {
                
                //ここはロックいらないはず
                IoN_prev = IoN_cur; //elseするの忘れてたために変なことになっていた可能性がある
                IoN_cur  = IoN_cur->next;
               
            }
        }
      
        
        
   
    }
   
   
}


//merge sort (head->nextから要素が入ってくる
//sort_resultに要素が次々に入る
static void mergeSort (struct image_data  *head, u_int size)  {

    struct image_data *data_left_head, *data_left, *data_right_head, *data_right_set;
    struct image_data *data_cur,       *data_prev;
    u_int        size_left_set, size_right_set;
    u_short      numl, numr;
   
    
    if ((data_right_head = (struct image_data *)malloc(sizeof(struct image_data))) == NULL) {
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
        

        mergeSort(data_left_head,  size_left_set );
        mergeSort(data_right_head, size_right_set);
        merge    (data_left_head, size_left_set, data_right_head, size_right_set, head);
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
static void merge (struct image_data  *list1_head, u_int size_left,
                   struct image_data  *list2_head, u_int size_right,
                   struct image_data  *original_head) {

    u_int              numl = 0, numr = 0, numAll = 0, sizeAll;
    struct image_data *data_cur, *data_prev, *data_left_cur,  *data_left_prev, *data_right_cur, *data_right_prev;
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
        if( numr >= size_right  || (numl < size_left && data_left_cur->sequenceNumber < data_right_cur->sequenceNumber)){
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

/* /\*ファイル加工用、JPGファイル作成用に使っていたが、今後使うかどうかは未定*\/ */

/* /\* ファイルに0のパディングを行う*\/ */
/* /\** */
/*  * @param tmp_filename */
/*  * @param num_of_pad */
/*  *\/ */
/* void PaddingZero (char *tmp_filename, u_int num_of_padding) { */
  
/*     FILE       *output_fp; */
/*     int i ; */
/*     u_char zero = 255; */
/*     u_int  padding_size = ONE_PACKET_SIZE*num_of_padding; */
   
/*     if((output_fp = fopen(tmp_filename, "a")) == NULL)  */
/*         err (EXIT_FAILURE, "cannot open :%s(for saving temporary image data.)\n", tmp_filename); */
   
/*     /\*printf(" %d Bytes ([data_buffer_size] %d * [num_of_padding] %d) padding\n" */
/* 	  ,ONE_PACKET_SIZE*num_of_padding */
/* 	  ,ONE_PACKET_SIZE */
/* 	  ,num_of_padding);*\/ */
/*     padding_size = num_of_padding * ONE_PACKET_SIZE; */
/*     for (i=0; i<padding_size; i++) { */
/*         fputc(zero,output_fp); */
/*     } */
/*     //fwrite(&zero, sizeof(u_char), padding_size, output_fp); */
   
/*     fclose(output_fp); */
/* } */


/* /\* JPG先端記号FFD8, FFE0の挿入*\/ */
/* /\** */
/*  * @param *tmp_filename: 書き込み先ファイル名 */
/*  * */
/*  *\/ */
/* static void Insert_FFD8_FFE0(char *tmp_filename) { */
/*     FILE       *output_fp; */
/*     u_short    htons_jpg_first     = htons (JPG_FIRST); */
/*     u_short    htons_jpg_type_zero = htons (JPG_TYPE_ZERO ); */

  
/*     if((output_fp = fopen(tmp_filename, "a")) == NULL)  {  */
/*         err (EXIT_FAILURE, "cannot open :%s(for saving temporary image data.)\n", tmp_filename); */
/*     } */
/*     if (fwrite (&htons_jpg_first,     sizeof(u_short), 1, output_fp) < 0 ) { //0xffd8 */
/*         err (EXIT_FAILURE, "cannot write file: %s\n",tmp_filename); */
/*     } */
/*     if (fwrite (&htons_jpg_type_zero, sizeof(u_short), 1, output_fp) < 0 ) { //0xffe0 */
/*         err (EXIT_FAILURE, "cannot write file: %s\n",tmp_filename); */
/*     } */

/*     fclose (output_fp); */
/* } */

/* /\* 作成する画像データに画像ファイル固有のヘッダを挿入する*\/ */
/* /\** */
/*  * @param *tmp_filename 現在データ保存中の一時ファイル */
/*  * @param *header       書き込むヘッダ */
/*  *\/ */
/* static void InsertHeader(char *tmp_filename, char *header_data, u_short size) { */
/*     FILE       *output_fp; */
/*     //  printf("Insert header\n"); */
  
/*     if((output_fp = fopen(tmp_filename, "a")) == NULL)  */
/*         err (EXIT_FAILURE, "cannot open :%s(for saving temporary image data.)\n", tmp_filename); */
   
/*     if (fwrite (header_data, sizeof(char),  size,  output_fp) < 0 )  //ヘッダの書き込み */
/*         err (EXIT_FAILURE, "cannot write header: %s\n",tmp_filename); */

/*     fclose(output_fp); */
/* } */
/* /\* 画像データファイルを作成する *\/ */
/* /\** */
/*  * @param clientIP: 送信元IPアドレス(ポインタ) */
/*  * @param processing_buffer_data: one_image_data型 受信画像データリストの要素 */
/*  * @param tmp_filename: 画像の一時保存ファイル名 char[]型 */
/*  *\/ */
/* static void CreateImage(char *clientIP,   */
/*                         struct one_image_data *processing_data,  */
/*                         char *tmp_filename)  */
/* { */

/*     char image_filename[100];                 /\* 生成JPGファイル名             *\/ */
/*     char mv_command  [200];                 /\* TENPファイル名前変更用コマンド *\/ */
/*     char format      [5]; */
  
/*     if(JPG_HANDLER) { */
/*         sprintf(format, "jpg"); */
/*     } else if (BMP_HANDLER) { */
/*         sprintf(format, "bmp"); */
/*     } */
/*     /\*画像ファイル作成*\/ */
/*     /\*作成画像ファイル名*\/ */
/*     sprintf (image_filename,  */
/*              "%s/Image_number%03u.%s" */
/*              ,clientIP */
/*              ,processing_data->image_number */
/*              ,format); */
/*     /\*  printf("%s\n" */
/*         ,image_filename);*\/ */
	   
/*     /\*一時ファイルから画像ファイルを作成*\/ */
/*     sprintf (mv_command, "mv %s %s\n" */
/*              ,tmp_filename */
/*              ,image_filename); */
/*     system(mv_command); */

/*     //画像作成中から作成終了フラグを立てる */
/*     processing_data->image_creating = 0; */

/* } */
