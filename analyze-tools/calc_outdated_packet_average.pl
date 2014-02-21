#!/usr/bin/perl

# 受信成功率算出区間で受信した転送データ量通知メッセージに記載されているシーケンス番号のパケットで
# ホップ数や通信品質などにより発生する遅延により当該算出区間までに受信されず、次回以降に受信されたものを
# 受信成功率算出区間毎に算出 (ノード毎に 合計も出す)

# 入力ファイル数はワイルドカード指定により何個でも指定可能とする

# --TODO --
# ipアドレス毎にセグメント毎or平均スループット計算するところ、重複しているので
# subにして関数化するべき

$usageStr = "<Usage>: perl calc_th_pernode_period_change.pl  (logfile_CISP_calcRecvRatio.log) (logfile_CISP_recvValNotifyMess.log) (logfile_CISP_recvImageData.log)\nEXIT... \n";

# 引数の種類
# (受信成功率算出ログ)(受信転送周期通知メッセージログ)(画像データパケット受信ログ)の
# 3種類
$expectedArg = 3;

if ( scalar(@ARGV) < $expectedArg) { #引数の数が不足したとき、プログラムを動作することができない
    die $usageStr;
}

printf("[all files %d\]\n", scalar(@ARGV));

# 各ログファイルのインデックス
$recvRatio_logfiles_first =  0;
$recvRatio_logfiles_end   =  scalar(@ARGV)/$expectedArg-1;
$recvMess_logfiles_first  =  scalar(@ARGV)/$expectedArg;
$recvMess_logfiles_end    = (scalar(@ARGV)/$expectedArg)*2-1;
$recvData_logfiles_first  = (scalar(@ARGV)/$expectedArg)*2;
$recvData_logfiles_end    =  scalar(@ARGV)-1;

#試行回数(受信成功率算出に使用する)
my $try_number = 0;
#受信スループット格納用ハッシュの初期化(perlではいらないけど一応)
%hash_addr = ();


# まず受信成功率算出ログから、受信成功率算出時刻を取得
# 次に、転送データ量通知メッセージログから、受信シーケンス範囲を取得
#（メッセージ飛びもありうるので、メッセージ毎に範囲選択するように）
# 最後に、画像データパケット受信ログから受信成功率算出区間からはみ出たパケット数とデータサイズを区間毎に計量


for (my $recvRatio_fileNum = $recvRatio_logfiles_first;
     $recvRatio_fileNum   <= $recvRatio_logfiles_end;
     $recvRatio_fileNum++) {
        
    #受信成功率算出ログファイルをオープン
    $recvRatio = $ARGV[$recvRatio_fileNum];

    #現在の転送周期をディレトリ名から取得
    # ファイルパス XXX/YYY/ZZZ/ ... , となっているので、がstat(adapt)XXsec_tryYYの
    # 名前まで探索
    @filename_field = split(/\//, $recvRatio);
    
    # まず、ディレクトリ名から初期広告周期を調べる。
    # 前回読んだファイル内容と同じ広告周期であれば、ファイル読みに入る。
    # 広告周期が異なる場合は、前回までに読んだ広告周期について、試行回数分のログから
    # 平均値を取る
    foreach $one_dirname (@filename_field) {
	
	if( $one_dirname =~ "sec" ) {  #match to "sec"
	    @target_dir_field = split(/\_/, $one_dirname);
	    $dir_second       = $target_dir_field[0]; #(stat or adapt)XXsecの部分
	    $dir_try          = $target_dir_field[1]; #tryYYの部分
	  
	    #ファイル名のXXXsec部分が異なる==試行回数全体での平均値算出タイミング
	    if (   $recvRatio_fileNum > $recvRatio_logfiles_first
	        && $dir_second ne $dir_second_old ) {
            
		printf("[outdated_packet_calculation],initial_sending_period,${dir_second_old},sec_result\n");
                #各IPアドレスごとに合計受信データ量をまとめる
		if ( scalar(%hash_addr) == 0 ) { #ハッシュ自体が空
		    printf("No data received\n");
		} else { #試行回数分の平均値を取る

		    #試行回数の表示
		    printf("all_try,%d\n",$try_number);
    
		    #ipアドレス出力
		    printf(",,,,,,,");#padding,camma
		    foreach $key_ipaddr ( sort keys %hash_addr) {
			printf("%s,,",${key_ipaddr});
		    }
		    printf("\n");
		    #$max_segment = $segment;
		    
		    for($segment = 1; $segment <= $max_segment; $segment++) {
			#送信周期の平均値算出
			$ave_timerOneSection{$segment} /= $try_number;
			#受信成功率の平均値算出
			$ave_rrOneSection{$segment}    /= $try_number;
			printf("segment,%d,sendingPeriod,%.3lf,sec,recv_ratio,%.3lf,"
			       ,$segment,$ave_timerOneSection{$segment},$ave_rrOneSection{$segment} );
			$ave_timerOneSection{$segment} = 0; #初期化
			$ave_rrOneSection{$segment}    = 0;

			printf("outdated_packet,");

			my $sum_num_outdate_pac = $sum_size_outdate_pac = 0.0;
			my $num_nodes = 0; #ノード数
			foreach $key_ipaddr ( sort keys %hash_addr) {
			    #試行回数分加算されているため、除算
			    my $num_outdate_pac   = $hash_addr{$key_ipaddr}{$segment}{'num_outdate_pac'}/$try_number; #outdateパケットの合計数
			    $sum_num_outdate_pac += $num_outdate_pac; #全ノード合計値
			    my $size_outdate_pac  = $hash_addr{$key_ipaddr}{$segment}{'size_outdate_pac'}/$try_number; #outdateパケットの合計サイズ
			    $sum_size_outdate_pac = $size_outdate_pac; #全ノード合計値
			    printf("num_outdatedPacket,%.3lf,size_outdatedPacket,"
				   , $num_outdatedPacket
				   , $size_outdatedPacket);
			    $num_nodes++;
			    
			}
			$sum_num_outdate_pac  /= $num_nodes;
			$sum_size_outdate_pac /= $num_nodes
			printf("all_num,%.3lf,all_size,%.3lf\n"
			       ,$sum_num_outdate_pac,$sum_size_outdate_pac);
		    }
		}
		#ハッシュの初期化(必須)
		%hash_addr = ();
		#試行回数を1に初期化	
		$try_number=1 

	    } else {
		#試行回数を加算する
		$try_number++; 
	    }
            # 前回算出したファイルの送信周期をOLDへ
	    $dir_second_old =  $dir_second;
	}

    }

#受信成功率算出ログを読む
#    printf("now reads recvRatio file,${recvRatio}\n");
    open IN, $recvRatio or die "cannot open $file ($!)";
    chomp(@line = <IN>);

    $segment = 1;  #受信成功率の算出回数（1から始まり、実験終了までの最大回数まで）
#    $numSend = 10; #受信成功率算出タイミング（画像転送回数の目安）

    foreach (@line) {
	@fields                           = split(/,+/, $_);
        #受信成功率算出タイミング(可変周期の結果では可変となるので注意)
	$deadlineOneSection{$segment}      = $fields[0]; #受信成功率算出時刻
	$ave_rrOneSection{$segment}       += $fields[2]; #受信成功率の平均値導出用

	# printf("one segment %d : %.3lf\n"
	# 	   ,$segment
	# 	   ,$deadlineOneSection{$segment});
	$segment++;
    }
    #segmentの最大値(ここで代入はあまりよくない。)
    $max_segment = $segment;
    close(IN);

    
    # 転送データ量通知メッセージログファイルのオープン
    #(引数にワイルドカード指定した場合でも、試行回数部分はソートされるので、
    # 上でオープンした受信成功率ログに対応する試行の転送周期ログがオープンされる
    $file = $ARGV[$recvMess_logfiles_first+$recvRatio_fileNum];
    open IN, $file or die "cannot open $file ($!)";
    chomp(@line = <IN>);
    $segment = 1;  #受信成功率の算出回数（1から始まり、実験終了までの最大回数まで）

    #転送データ量通知メッセージ数の初期化
    @num_notifyMessOneSegment = ();
    

    foreach (@line) {
	@fields            = split(/ +/, $_);
	#送信元IPアドレス
	$from_ipaddr       = "${fields[9]}";
	
	
        #送信元IPアドレス毎にメッセージ中のシーケンスfrom endを格納する
	$hash_addr{$from_ipaddr}{$segment}{$num_notifyMessOneSegment{$from_ipaddr}}{'seq_from'} = $fields[2]; #シーケンス番号はじめ
	$hash_addr{$from_ipaddr}{$segment}{$num_notifyMessOneSegment{$from_ipaddr}}{'seq_end'}  = $fields[4]; #シーケンス番号はじめ

	# printf("ipaddr,%s,from,%d,end,%d\n"
	#        ,${from_ipaddr}
	#        ,${hash_addr{$from_ipaddr}{'seq_from'}{$segment}{$num_notifyMessOneSegment{$from_ipaddr}}}
	#        ,${hash_addr{$from_ipaddr}{'seq_end'} {$segment}{$num_notifyMessOneSegment{$from_ipaddr}}});
	
	$num_notifyMessOneSegment{$from_ipaddr}++; #1セグメントの間に受信したメッセージ総数
	$segment++;
    }
   
    close(IN);
    

     #受信成功率算出ファイルと対応する受信パケットデータログ
    $file = $ARGV[$recvData_logfiles_first+$recvRatio_fileNum];
    
#    printf("now reads recvPacData file,${file}\n");
    
    open IN, $file or die "cannot open $file ($!)";
    chomp(@line = <IN>);

   
    # 受信パケットログからoutdateしたパケットの数とそのデータサイズを加算する
    # 第一回目のセグメントでは、outdateしたパケットを計量せず、トップ行へ戻る
    # (配列@lineはファイルの全行、$_ は１パケット分の受信ログ)

    foreach (@line) {
	@fields              = split(/ +/, $_);
	#送信元IPアドレス
	$ipaddr              = "$fields[5]";
	#パケット受信時刻                                                                                                     
	$time                =  $fields[0];
	#送信データサイズ
	$datasize_onesegment =  $fields[3];

	#受信成功率算出タイミングで、セグメントのインデックスをインクリメント
	if ( $time >= $deadlineOneSection{$segment}) {
	    $segment++;
	}

	if ($segment > 1) {  
	    #outdateパケットの計量
	    foreach ($hash_addr{$ipaddr}{$segment}{$num_notifyMessOneSegment{$from_ipaddr}}{'seq_from'}) {
	    
	    }
	}
	
    } #1つの受信ログファイルについて計算完了
    
    close(IN);


} # END of loop


# 引数のうち、最後のファイルを読み込んだ場合、ここでそれまでの試行における
# 平均受信スループットを算出する

if ( scalar(%hash_addr) == 0) { #ハッシュ自体が空
    printf("No data received\n");
} else {

    #ipアドレス出力	
    #printf("segment_number,");
    printf("initial_sending_period,${dir_second_old},sec_result\n");
    printf("all_try,%d\n",$try_number);
    printf(",,,,");
    foreach $key_ipaddr ( sort keys %hash_addr) {
	printf("ipaddr,%s,",${key_ipaddr});
    }
    printf("\n");
    #$max_segment = $segment;
    #ノードIP毎に平均受信スループットを算出
    #ipアドレス出力
    for($segment = 1; $segment <= $max_segment; $segment++) {
		

#送信周期の平均値算出
	$ave_deadlineOneSection{$segment} /= $try_number;
	$ave_rrOneSection{$segment}    /= $try_number;
	printf("segment,%d,sendingPeriod,%.3lf,sec,recv_ratio,%.3lf,"
	       ,$segment,$ave_deadlineOneSection{$segment},$ave_rrOneSection{$segment} );
	foreach $key_ipaddr ( sort keys %hash_addr) {
	    #試行回数分加算されているため、除算
	    my $one_throuhput = $hash_addr{$key_ipaddr}{$segment}{'th'}/$try_number;
	    printf("%.3lf,kbps,", $one_throuhput);
	}
	

	$one_throuhput = $th_sum{$segment}/$try_number;
	printf("all,%.3lf,kbps\n",$one_throuhput );
	$th_sum{$segment} = 0;

    }
}
exit;
