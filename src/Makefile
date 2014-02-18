# ----------------
# make時の注意
# 基本的に、Cファイルを更新したら
# 	$ make
# するだけでよい。更新したCファイルのみ、オブジェクトファイルが生成される

# ただし、ファイル「common.h」のパラメータを変えた場合、
# 	$ make clean; make
# とすること

# スレッドIDを標準出力でだすために、警告が多数出るが、問題ない
# (それ以外で警告が出たら、該当部分を見直すこと)

#コンパイラ、コンパイルオプション
CC	        = gcc
#デバッグするためには最適化オプションしないuほうがよい
# 終わったら -O0オプション消すこと(-O0は最適化せずにコード入れ替えしないオプション)
CFLAGS	        = -g -O0 -lpthread -lrt -lm
#CFLAGS	        = -g  -lpthread -lrt -lm

#オブジェクトファイルの組み合わせ
CLIENT_OBJGROUP                     = clientImageSender.o packetSender.o
SERVER_OBJGROUP                     = serverImageReceiver.o packetSender.o createImage.o
SERVER_TEST_STATIC_PERIOD_OBJGROUP  = serverImageReceiver_testStat.o packetSender.o createImage.o
#実行ファイル名(画像送信クライアント、画像受信サーバ、画像受信サーバ（固定周期の最適値確認用）
EXECUTABLE_CLIENT           = clientd
EXECUTABLE_SERVER           = serverd
EXECUTABLE_SERVER_TEST_STAT = serverd_test_static_period

#クライアント、サーバ両方の実行ファイルを作成する
all:clientImageSender serverImageReceiver serverImageReceiver_testStat
#all: clientImageSender

#クライアントプログラム(画像転送ノード用)
clientImageSender:${CLIENT_OBJGROUP}
	$(CC)  ${CLIENT_OBJGROUP} -o ${EXECUTABLE_CLIENT} ${CFLAGS}
#サーバプログラム(画像収集ノード用)
serverImageReceiver:${SERVER_OBJGROUP}
	$(CC)  ${SERVER_OBJGROUP} -o ${EXECUTABLE_SERVER} ${CFLAGS}
serverImageReceiver_testStat:${SERVER_TEST_STATIC_PERIOD_OBJGROUP}
	$(CC)  ${SERVER_TEST_STATIC_PERIOD_OBJGROUP} -o ${EXECUTABLE_SERVER_TEST_STAT} ${CFLAGS}

clean:
	\rm *.o *~ ${EXECUTABLE_SERVER} ${EXECUTABLE_SERVER_TEST_STAT} ${EXECUTABLE_CLIENT}
