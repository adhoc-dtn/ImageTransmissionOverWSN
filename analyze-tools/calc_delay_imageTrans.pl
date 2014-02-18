#!/usr/bin/perl

# CISPログ(受信データ)を入力ファイルとする
# $ perl calc_delay_from_logger.pl (周期方式 static or adaptive) (基地局側のCISPログdir)/ (station側のログdir)/
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



if ( scalar(@ARGV)<2  ) {
   die "<Usage>: perl calc_delay_from_logger.pl (control type: adapt or stat)  (server log1: recv_packet ) (server log2 [if type is \"adapt\"] : recv_ratio ).\n";
}

#実験タイプ
$exp_type = $ARGV[0]; 
if ($exp_type eq "adapt" && scalar(@ARGV) != 3) {
    die "<Usage>: perl calc_delay_from_logger.pl (control type: adapt or stat)  (server log1: recv_packet ) (server log2 [if type is \"adapt\"] : recv_ratio ).\n";
}
    

#delay合計値
$amount_delay          = 0.0;
#補正delay
$amount_delay_correct  = 0.0;

#delay平均値
$average_delay         = 0.0;
$average_delay_correct = 0.0;


# 使用アドレスを 192.168.30.XXX/24(有線 ) 192.168.50.XXX(無線)とする前提
# ネットワークアドレス(3 bytes目にあたる部分)
$WIRELESS_LAN_IF       = 50;
$WIRED_LAN_IF          = 30;


#ログ記録開始周期
# $period_initial = 10;
# $period_adder   = 1;

#ファイル数
#$numFiles = 30;

#送信側ログファイル
$client_logfilename    = "logfile_imageSender_sentData.log";
$server_logfilename    = "logfile_CISP_recvImageData.log";
#受信成功率算出までの画像転送回数
$numSend     = 10; 
#送信端末数
$numNodes    = 10; 
#総受信成功率算出回数(固定周期のみ初期化必要)
$all_segment = 5;


#平均遅延時間の算出間隔
if($exp_type eq "stat") { 	# 固定周期の場合は算出間隔固定値
    #$calc_period = $numSend;
    $max_segment = $all_segment;
    $arg_offset  = 1; #受信パケット保存ファイルの引数番号-1(以降のファイルを処理する)

    $client_logdir         = "Static_log"; #送信側ログ保存ディレクトリ
} elsif ($exp_type eq "adapt") {
    #設定周期を取得し、受信成功率算出間隔を取る
    $recvRatio = $ARGV[1];
    open IN, $recvRatio or die "cannot open $file ($!)";
    chomp(@line = <IN>);

    $segment = 1;

    foreach (@line) {
	@fields                  = split(/,+/, $_);
	$timerOneSection{$segment} = $numSend*$fields[8]; #転送周期の設定
	printf("one segment %d : %.3lf\n"
		   ,$segment
		   ,$timerOneSection{$segment});
	$segment++;
    }
    $max_segment = $segment-1;
    close(IN);

    $client_logdir         = "Adapt_log"; #送信側ログ保存ディレクトリ

    $arg_offset  = 2; #受信パケット保存ファイルの引数番号-1(以降のファイルを処理する)
} else {
    die "<Usage>: perl calc_delay_from_logger.pl (control type: adapt or stat)  (server log1: recv_packet ) (server log2 [if type is \"adapt\"] : recv_ratio ).\n";

}

#引数のうち、サーバのデータ受信ログだけを抽出(type、adaptの場合の周期変化は考慮に入れない)
@server_logs = splice( @ARGV, $arg_offset); 

$logfile = "delaylog.csv";
#ログファイル作成用
open OUTLOG, ">> ${logfile}" or die "cannot open $logfile ($!)";

#遅延算出フェーズ
foreach $server_file (@server_logs) { 
    #ノード毎の遅延時間格納用ハッシュ初期化（複数ファイル同時算出時を考慮）
    %hash_addr = ();
    $target_file  = $server_file;

   

    open TARGET_IN, $target_file or die "cannot open $target_file ($!)";
    chomp(@target_line = <TARGET_IN>);
    printf(OUTLOG "filename filename %s\n", $target_file);
    printf("filename %s\n", $target_file);

    $ip_in_hash     = 0;
    $endOfCalcDelay = 0;

    #1つのサーバのログファイルについて、１端末ずつ遅延計算を開始する
    ${countNodes} = 0;
    while($countNodes < $numNodes) {
#	printf("now calc ${countNodes} nodes\n");
	#遅延算出対象IPアドレスを決定
	foreach ( @target_line ) {
	    $ip_in_hash = 0;
	    s/[\r\n]//g;
	    @target_field  = split(/ +/, $_);

	    #送信元IP
	    $TARGET_ipaddr    = $target_field[5];             #無線のIPが入る(XXX.XXX.50.XXX)
	    $TARGET_ipaddr    
		=~ s/.${WIRELESS_LAN_IF}./.${WIRED_LAN_IF}./; #有線のIPに置換(XXX.XXX.30.XXX)

	    
	    if ( scalar(%hash_addr)     == 0) { #ハッシュが空
		$ip_in_hash = 0;
	    } else { 
		while (($key, $ref) = each(%hash_addr)) {
		    if ($key eq $TARGET_ipaddr) { #ハッシュの中にip発見
			$ip_in_hash = 1;
		    } 
		}
	    }

	    if ($ip_in_hash == 0) {
		last;
	    }
	}
	
        #まだ遅延計算していないクライアントについて処理
	if($ip_in_hash == 0) {  
	    $countNodes++;
			
	    #受信成功率算出区間の最初の時刻を記録
	    $time_first = -1;
	    $segment    =  1;
	    
	    if($exp_type eq "stat") { 	# 固定周期の場合は算出間隔固定値
		$timer_calc = $calc_period;
	    } else {
		$timer_calc = $timerOneSection{$segment};# 可変周期の場合は受信統計ファイルから算出間隔を算出
	    }

	    #サーバログファイル開いて、target ip アドレスについての遅延計算
	    open SERVER_IN, $server_file or die "cannot open $server_file ($!)";
	    chomp(@server_line = <SERVER_IN>);
	    

	    #サーバのIPアドレスをディレトリ名から取得
	    # ファイルパス XXX/YYY/ZZZ/ ... , となっているので、クライアントログのパスまで探索
	    @filename_field = split(/\//, $server_file);
	    
	    #サーバIPアドレス抽出
	    foreach $one_dirname (@filename_field) {
		$client_dir_name_old = $client_dir_name;
		$client_dir_name = $one_dirname;
		#printf("line,@filename_field , ${client_dir_name_old},${client_dir_name},${client_logdir}\n");

		if($client_dir_name_old eq $client_logdir) { 
		    #ログ保存ディレクトリと一致したら、client_dir_nameについてsplitしてipアドレス抽出
		    @addr_field = split(/_+/, $client_dir_name);
		    $DEST_ipaddr  = $addr_field[1];
		    
		}elsif($client_dir_name =~ "sec") { 
		    $static_period = substr($client_dir_name,4,2);
		   
		    if ($exp_type eq "stat") {
			$timer_calc = $numSend*$static_period;
		    }
		    #printf("static period ${static_period}, calc period ${timer_calc}\n");
		}

	    }
	    #printf("serveraddr ${DEST_ipaddr}\n");
	    $client_file = $server_file; #サーバのログファイル名
	    $client_file =~ s/${DEST_ipaddr}/${TARGET_ipaddr}/; #送信元IPアドレスに置換
	    $client_file =~ s/${server_logfilename}/${client_logfilename}/; #送信元IPアドレスに置換
	    printf("now open ${client_file}\n");

	    #対応するクライアントipの送信ログファイルをオープン
	    open CLIENT_IN, $client_file or die "cannot open $client_file ($!)";
	    chomp(@client_line = <CLIENT_IN>);
	    
	    foreach ( @server_line )  {
		s/[\r\n]//g;
		@server_field  = split(/ +/, $_);
		
		#送信元IP
		$SERVER_ipaddr    = $server_field[5];             #無線のIPが入る(XXX.XXX.50.XXX)
		$SERVER_ipaddr    
		    =~ s/.${WIRELESS_LAN_IF}./.${WIRED_LAN_IF}./; #有線のIPに置換(XXX.XXX.30.XXX)

		#遅延算出対象のアドレスからのパケット受信時刻のみを統計
		if( $SERVER_ipaddr eq $TARGET_ipaddr) {
		    #パケット受信時刻
		    $SERVER_time      = $server_field[0];

		    if ($time_first == -1) { #ファイル開き始めでは、最初の行の時刻を区間最初の時刻として使用
			$time_first = $SERVER_time;

		    } elsif ($SERVER_time - $time_first >= $timer_calc) { #1セグメントの平均遅延算出
			
			if ($hash_addr{$SERVER_ipaddr}{'num_packet'} > 0) {
			    $hash_addr{$SERVER_ipaddr}{$segment}{'ave_delay'} 
			    = $hash_addr{$SERVER_ipaddr}{'per_packet_delay_sum'}/$hash_addr{$SERVER_ipaddr}{'num_packet'};
			printf("calcnum,${segment},ip,${SERVER_ipaddr},numpac,$hash_addr{$SERVER_ipaddr}{'num_packet'},delaysum,$hash_addr{$SERVER_ipaddr}{'per_packet_delay_sum'},delay,${hash_addr{$SERVER_ipaddr}{$segment}{'ave_delay'}}\n");
			} else {
			    $hash_addr{$SERVER_ipaddr}{$segment}{'ave_delay'} = 0;
			}
			
			#初期化
			$hash_addr{$SERVER_ipaddr}{'per_packet_delay_sum'} = 0;
			$hash_addr{$SERVER_ipaddr}{'num_packet'}           = 0;
			
			$segment++;
			if($exp_type eq "adapt") { 	# 可変周期は算出間隔更新
			    $timer_calc = $timerOneSection{$segment};

			}             #固定周期(stat)の場合は算出間隔固定値

			#算出が終わったらserver timeを新たに区間最初の時刻にする
			$time_first = $SERVER_time;
		    }


		    # フレームシーケンス番号
		    $SERVER_image_sequence   = $server_field[7];
		    # パケットシーケンス番号
		    $SERVER_packet_sequence  = $server_field[9];

		    #以降、クライアントIPのログファイルを読んで、対応シーケンスを探して遅延時間を加算する

		    
		    
		    @sub_client_line = splice( @client_line, $hash_addr{$SERVER_ipaddr}{'line_number'}); 
		    @client_line = @sub_client_line; #次回以降、前回参照部分を切り出し対象にする	
		    #printf("sub_client_line,${sub_client_line[0]}\n");

		    $file_counter= 0;
		    foreach $one_line (@sub_client_line) {
		    
			#行から改行消去
			s/[\r\n]//g;
			
			@client_field           = split(/ +/,$one_line);
			#シーケンス番号
			$CLIENT_image_sequence  = $client_field[5];
			$CLIENT_packet_sequence = $client_field[7];
			$file_counter  += 1;
			#printf("segment,${segment},ip,${SERVER_ipaddr},(seq_im,seq_pac),(${SERVER_image_sequence},${SERVER_packet_sequence}),(seq_im,seq_pac),(${CLIENT_image_sequence},${CLIENT_packet_sequence}),${file_counter}\n");
			#画像シーケンス番号・パケット番号の検知
			if ( (${SERVER_image_sequence}  eq  ${CLIENT_image_sequence})
			     && ${SERVER_packet_sequence} eq  ${CLIENT_packet_sequence}) {

			    #送信時刻(送信側での時刻)
			    $CLIENT_time     = $client_field[0];
			    #遅延時間を加算(受信パケット数で割って平均遅延出す)
			    $delay = $SERVER_time - $CLIENT_time;
			    $hash_addr{$SERVER_ipaddr}{'per_packet_delay_sum'} = $hash_addr{$SERVER_ipaddr}{'per_packet_delay_sum'} + $delay;
			    #受信パケット数のカウント
			    $hash_addr{$SERVER_ipaddr}{'num_packet'}       += 1;
			    #ファイルの行番号を記録(次回移行はここから走査)
			    $hash_addr{$SERVER_ipaddr}{'line_number'}       = $file_counter;
			    #printf("segment,${segment},ip,${SERVER_ipaddr},(seq_im,seq_pac),(${SERVER_image_sequence},${SERVER_packet_sequence}),${SERVER_time},${CLIENT_time}delay,${delay},delaysum,$hash_addr{$SERVER_ipaddr}{'per_packet_delay_sum'},${hash_addr{$SERVER_ipaddr}{'line_number'}}\n");
			    
			    last;
			} elsif ( ${SERVER_image_sequence}  lt  ${CLIENT_image_sequence}) { #送信ログにないパケットが受信されたことになり、ここにくることはない
			    last;
			}
		    }
		}
	    } #1つのクライアントについての計算が終わり

	    close CLIENT_IN;
	    close SERVER_IN; 
	    # 最後のsegmentについては上で統計されないことがあるため、算出
	    
	    if ( $segment < $max_segment) {
		foreach $key_ipaddr ( sort keys %hash_addr) {
		    $hash_addr{$key_ipaddr}{$segment}{'ave_delay'} 
		    = $hash_addr{$key_ipaddr}{'per_packet_delay_sum'}/$hash_addr{$key_ipaddr}{'num_packet'};
		}
		$segment++;
	    }
	    

	}
    } #1つのサーバファイルについての計算が終わり
    printf ("---file. ${server_dir_name},delayCalculation---\n",);
    $ave_delay_all = 0;
    for($segment = 1; $segment <= $max_segment; $segment++) {
	printf("%d,",$segment);
	printf(OUTLOG "${segment},");
	
	$ave_segment_delay = 0.0; #IPアドレス平均
	$num_client        = 0;
	foreach $key_ipaddr ( sort keys %hash_addr) {
	    $ave_segment_delay += $hash_addr{$key_ipaddr}{$segment}{'ave_delay'};
	    printf("ip,${key_ipaddr},delay,${hash_addr{$key_ipaddr}{$segment}{'ave_delay'}},");
	    printf(OUTLOG "ip,${key_ipaddr},delay,${hash_addr{$key_ipaddr}{$segment}{'ave_delay'}},");
	    
	    $num_client++;
	}
	
	$ave_segment_delay /= $num_client;
	$ave_delay_all += $ave_segment_delay;
	printf("segment_average_delay,${ave_segment_delay}\n");
	printf(OUTLOG "segment_average_delay,${ave_segment_delay}\n");

    }
    close TARGET_IN; #サーバログ１つ分の終了
    $ave_delay_all /= $max_segment;
    printf("all_segments_average_delay,${ave_delay_all}\n");
    printf(OUTLOG "all_segments_average_delay,${ave_delay_all}\n");
	
    
} #全ログファイルについて算出処理を繰り返し、終了
close(OUTLOG);
exit;




