#!/usr/bin/perl

# 受信成功率算出区間までの各ノード毎の平均キュー長を算出する(単位は packet)

# --TODO --
# このスクリプトはまだ未完性で機能しない
# ipアドレス毎にキュー長計算するところ、重複しているので
# subにして関数化するべき


#入力ファイル数はワイルドカードなどで何個でも指定可能とする

if ( scalar(@ARGV)<2) {
    die "<Usage>: perl calc_th_pernode_period_change.pl  (logfile_CISP_calcRecvRatio.log of server file) (logfile_enqueue.log ...(multiple files available)\nEXIT... \n";
}

printf("[all files %d\]\n",scalar(@ARGV));
# 最初のログファイルのインデックス(受信成功率ファイルのみエンドを保存)
$recvRatio_logfiles_first = 0;
$recvData_logfiles_first  = 1;


#試行回数(受信成功率算出に使用する)
my $try_number = 0;
#受信スループット格納用ハッシュの初期化(perlではいらないけど一応)
%hash_addr = ();

#まず受信成功率算出ログから、受信成功率算出までの時間を取得
#次に、各受信成功率算出までの時間間隔で、平均スループットを算出する
#各算出タイミング(segment)の平均値を取る
for (my $recvRatio_fileNum = $recvRatio_logfiles_first;
     $recvRatio_fileNum   <= $recvRatio_logfiles_end;
     $recvRatio_fileNum++) {
    
    
    $recvRatio = $ARGV[$recvRatio_fileNum];

    #現在の転送周期をディレトリ名から取得
    # ファイルパス XXX/YYY/ZZZ/ ... , となっているので、がstat(adapt)XXsec_tryYYの
    # 名前まで探索
    @filename_field = split(/\//, $recvRatio);
    
    # ディレクトリ名から初期広告周期を調べる。同じ広告周期であるものは
    # 試行回数分の平均受信スループットを計算する
    foreach $one_dirname (@filename_field) {
	

	if( $one_dirname =~ "sec" ) {  #match to "sec"
 
	    @target_dir_field = split(/\_/, $one_dirname);
	    $dir_second       = $target_dir_field[0]; #(stat or adapt)XXsecの部分
	    $dir_try          = $target_dir_field[1]; #tryYYの部分
	  
	    
	    #ファイル名のXXXsec部分が異なる==試行回数全体での平均値算出タイミング
	    if (   $recvRatio_fileNum > $recvRatio_logfiles_first
	        && $dir_second ne $dir_second_old ) {
                printf("initial_sending_period,${dir_second_old},sec_result\n");
                #各IPアドレスごとに合計受信データ量をまとめる
		if ( scalar(%hash_addr) == 0) { #ハッシュ自体が空
		    printf("No data received\n");
		} else {

		    #試行回数の表示
		    printf("all_try,%d\n",$try_number);
    
		    #ipアドレス出力
		    printf(",,,,,,,");
		    foreach $key_ipaddr ( sort keys %hash_addr) {
			printf("%s,,",${key_ipaddr});
		    }
		    printf("\n");
		    #$max_segment = $segment;
		    
		    for($segment = 1; $segment <= $max_segment; $segment++) {
			#送信周期の平均値算出
			$ave_timerOneSection{$segment} /= $try_number;
			$ave_rrOneSection{$segment}    /= $try_number;
			printf("segment,%d,sendingPeriod,%.3lf,sec,recv_ratio,%.3lf,"
			       ,$segment,$ave_timerOneSection{$segment},$ave_rrOneSection{$segment} );
			$ave_timerOneSection{$segment} = 0; #初期化
			$ave_rrOneSection{$segment}    = 0;

			foreach $key_ipaddr ( sort keys %hash_addr) {
			    #試行回数分加算されているため、除算
			    my $one_throuhput = $hash_addr{$key_ipaddr}{'th'}{$segment}/$try_number; #0除算にならないはず(なると止まる)
			    printf("%.3lf,kbps,", $one_throuhput);
			}
			$one_throuhput = $th_sum{$segment}/$try_number;
			printf("all,%.3lf,kbps\n",$one_throuhput );
			$th_sum{$segment} = 0;
		    }
		}
		#ハッシュの初期化(必須)
		%hash_addr = ();
		#試行回数を0に初期化	
		$try_number=1 
	    } else {
		#試行回数を加算する
		$try_number++; 
	    }
            # 前回算出したファイルの送信周期をOLDへ
	    $dir_second_old =  $dir_second;
	}

    }

#    printf("now reads recvRatio file,${recvRatio}\n");
    open IN, $recvRatio or die "cannot open $file ($!)";
    chomp(@line = <IN>);

    $segment = 1;  #受信成功率の算出回数（1から始まり、実験終了までの最大回数まで）
    $numSend = 10; #受信成功率算出タイミング（画像転送回数の目安）

    foreach (@line) {
	@fields                  = split(/,+/, $_);
        #受信成功率算出タイミング(可変周期の結果では可変となるので注意)
	$timerOneSection{$segment}      = $numSend*$fields[8]; 
	$ave_timerOneSection{$segment} += $fields[8]; 
	$ave_rrOneSection{$segment}    += $fields[2]; 

	# printf("one segment %d : %.3lf\n"
	# 	   ,$segment
	# 	   ,$timerOneSection{$segment});
	$segment++;
    }
    #segmentの最大値(ここで代入はあまりよくない。)
    $max_segment = $segment;
    close(IN);
    # 受信成功率算出間隔の保存

     #受信成功率算出ファイルと対応する受信パケットデータログ
    $file = $ARGV[$recvData_logfiles_first+$recvRatio_fileNum];
    
#    printf("now reads recvPacData file,${file}\n");
    
    open IN, $file or die "cannot open $file ($!)";
    chomp(@line = <IN>);

    $isTimeFirst = 1;
    $first = 0;
    # 受信パケットログから受信データを受信成功率算出時間まで加算
    # 受信成功率算出時間のタイミングで[データサイズ/算出時間]で受信スループットを計算
    # 計算は各ノード毎に行う。全ノードの合計スループットも算出

    # @lineはファイル全行、$_ は１パケット分の受信ログ
    foreach (@line) {
	@fields              = split(/ +/, $_);
	#送信元IPアドレス
	$ipaddr              = "$fields[5]";
	#パケット受信時刻
	$time                =  $fields[0];
	#送信データサイズ
	$datasize_onesegment =  $fields[3];


	#ログの最初の行の時間からスループットを算出する
	if ($isTimeFirst ==1) { 
	    $time_segment_first     = $time;
	    $segment                = 1;
	    $isTimeFirst            = 0;
	}
	
	
	#受信成功率時間経ったらすべてのノードにおいてスループット算出->加算
	if($segment < $max_segment 
	   &&   $time - $time_segment_first >= $timerOneSection{$segment}) {

	    foreach $key_ipaddr ( sort keys %hash_addr) {
            #合計受信データ量を受信成功率算出間隔で割ると受信スループットとなる
		#1ノードからのスループット
		my $one_th = ($hash_addr{$key_ipaddr}{'data_size_one_sec'}*8.0)/($timerOneSection{$segment}*1000);
		$hash_addr{$key_ipaddr}{'data_size_one_sec'}  = 0; #受信データサイズ初期化

		$hash_addr{$key_ipaddr}{'th'}{$segment}     += $one_th; 	#1ノードのスループット算出用
		$th_sum{$segment}                           += $one_th;         #ノード全体の受信スループット算出用
		#printf("${segment}allsum,%.3lf,ip${key_ipaddr},th,%.3lf\n",$th_sum{$segment},$one_th );
	    }
	    $time_segment_first = $time;
	    $segment++;
	    
	}
	#受信データサイズの加算(送信IPアドレス毎に集計)
	$hash_addr{$ipaddr}{'data_size_one_sec'}  += $datasize_onesegment;
	

    } #1つの受信ログファイルについて計算完了

    #一番最後のセグメントは、スループット算出前にループを抜けている場合があるので
    #ここで算出
    if($segment < $max_segment && ($time - $time_segment_first) > 0 ) {
	foreach $key_ipaddr ( sort keys %hash_addr) {
	    
	    my $one_th = ($hash_addr{$key_ipaddr}{'data_size_one_sec'}*8.0)/(($time - $time_segment_first)*1000);
	    $hash_addr{$key_ipaddr}{'data_size_one_sec'}  = 0; #受信データサイズ初期化

	    $hash_addr{$key_ipaddr}{'th'}{$segment}     += $one_th; 	#1ノードのスループット算出用
	    $th_sum{$segment}                           += $one_th;     #ノード全体の受信スループット算出用
	}
    }


    close(IN);
} 				# END of loop


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
	$ave_timerOneSection{$segment} /= $try_number;
	$ave_rrOneSection{$segment}    /= $try_number;
	printf("segment,%d,sendingPeriod,%.3lf,sec,recv_ratio,%.3lf,"
	       ,$segment,$ave_timerOneSection{$segment},$ave_rrOneSection{$segment} );
	foreach $key_ipaddr ( sort keys %hash_addr) {
	    #試行回数分加算されているため、除算
	    my $one_throuhput = $hash_addr{$key_ipaddr}{'th'}{$segment}/$try_number;
	    printf("%.3lf,kbps,", $one_throuhput);
	}
	

	$one_throuhput = $th_sum{$segment}/$try_number;
	printf("all,%.3lf,kbps\n",$one_throuhput );
	$th_sum{$segment} = 0;

    }
}
exit;
