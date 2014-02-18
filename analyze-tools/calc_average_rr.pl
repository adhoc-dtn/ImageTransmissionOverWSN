#!/usr/bin/perl

# $ perl calc_average_rr.pl
# 
# [注意]実験用の送受信端末は、実験前後に時刻同期しければ、このプログラムから出力される遅延データは意味をなさないでしょう
#       (recursive_XXX.plスクリプトは NICTのNTPサーバで時刻同期を取るようになっています)
#        ただし、屋外実験など、ntpサーバと通信不可能な状況では遅延データは取れないと思います
#        (GWを用意して実験開始前に時刻同期する方法もありますが)
# 
# [注意]　受信成功率算出タイミング　実験時の値を入力してください
#------------------------
# ありゅごりじゅむ
# まず、サーバの受信ログを開いて、送信元IPアドレスをきろく
# 次に、サーバの受信ログからその送信元IPアドレスについてのログのみを抽出し、
# 対応するクライアントIPアドレスの送信ログから遅延計算する。
# 受信時刻-送信時刻をだす。
# これを全端末について繰り返す

# 処理項目が多い場合、ファイルのオープン・クローズを繰り返すとオーバヘッドで処理速度が落ちるので気を付ける

#use strict;
#use warnings;
use utf8;
use open ":utf8";
binmode STDIN, ':encoding(cp932)';
binmode STDOUT, ':encoding(cp932)';
binmode STDERR, ':encoding(cp932)';



$logfile = "recvRatioLog.csv";
#ログファイル作成用
open OUTLOG, ">> ${logfile}" or die "cannot open $logfile ($!)";


#遅延算出フェーズ
foreach $server_file (@ARGV) { 
    #ノード毎の遅延時間格納用ハッシュ初期化（複数ファイル同時算出時を考慮）
    %hash_addr = ();

    open SERVER_IN, $server_file or die "cannot open $server_file ($!)";
    chomp(@server_line = <SERVER_IN>);
    
    printf("filename %s\n", $server_file);

    $num_lines      = 0;
    $recv_ratio_sum = 0;
    foreach $one_line (@server_line) { 
	@fields          = split(/,+/, $one_line);
	#printf("[read line] @{fields}\n");
	$recv_ratio_sum += $fields[2]; #受信成功率
	$sending_period  = $fields[8]; #転送周期
	$num_lines++;
    }
    $ave_recv_ratio = $recv_ratio_sum/$num_lines;

    printf("priod,${sending_period},average,${ave_recv_ratio}\n");
    printf(OUTLOG "priod,${sending_period},average,${ave_recv_ratio}\n");

    close SERVER_IN; #サーバログ１つ分の終了
} #全ログファイルについて算出処理を繰り返し、終了

close(OUTLOG);

exit;




