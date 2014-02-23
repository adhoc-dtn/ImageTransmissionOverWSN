#!/usr/bin/perl

#はじめて受信成功率が混雑検知しきい値を超えた時間から、
#画像総送信回数のうち、画像データ受信に成功した割合を求める
# (この割合は、画像データ1枚を構成するデータを100%受信の他、
# しきい値を段階的に設定することもできる)

# 入力ファイル数はワイルドカード指定により何個でも指定可能とする



$usageStr = "<Usage>: perl calc_successful_image_sending_ratio_after_steady.pl  (logfile_CISP_calcRecvRatio.log) (logfile_CISP_recvImageData.log)\nEXIT... \n";

# 引数の種類
# (受信成功率算出ログ)(画像データパケット受信ログ)の
# 3種類
$expectedArg = 2;

if ( scalar(@ARGV) < $expectedArg) { #引数の数が不足したとき、プログラムを動作することができない
    die $usageStr;
}

printf("[all files %d\]\n", scalar(@ARGV));

# 各ログファイルのインデックス
#受信成功率ログのインデックス（はじめとおわり）
$recvRatio_logfiles_first =  0;
$recvRatio_logfiles_end   =  scalar(@ARGV)/$expectedArg-1;

#受信データログ
$recvData_logfiles_first  = scalar(@ARGV)/$expectedArg;
$recvData_logfiles_end    = scalar(@ARGV)-1;


#--以下の項目、プログラム実行前に初期化の必要あり
# 混雑検知しきい値
my $threashold_cong = 0.9;
# 画像を構成するパケットの総数(送信画像データにより変更する必要あり)
my $num_segment_of_one_image = 312; # sequence start with 0 and end with 311

# 下の例のように、最低受信しきい値0.1、adder0.05の場合
# まず画像データ１枚のうち、データサイズが１割以上の画像枚数の割合を求めた後、
# 0.15, 0.20, 0.25　以上の割合を求め、最終的に1を超えないしきい値すべてで
# 画像受信割合を算出する。

# 画像受信割合の最低しきい値
my $min_threash_one_image = 0.1;
# 画像受信割合のしきい値変化
my $adder_thread          = 0.05;
# 最大値(100%の意味。ここは変更不可)
my $max_threash_one_image = 1.0;



#試行回数(受信成功率算出に使用する)
my $try_number = 0;
#受信スループット格納用ハッシュの初期化(perlではいらないけど一応)
%hash_addr     = ();



# --プログラムの簡単な流れ--
# まず受信成功率算出ログから、はじめて混雑検知しきい値を超える受信成功率算出が
# 行われたときの時刻を取得する。
# 次に、その時刻以降のログに置いて、画像データパケット受信ログから
# 画像データ１枚のうち、何割が受かっているかという統計を取る。
# (ノード毎に取る)


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
		printf("hoge\n");
                # #各IPアドレスごとに合計受信データ量をまとめる
		# if ( scalar(%hash_addr) == 0 ) { #ハッシュ自体が空
		#     printf("No data received\n");
		# } else { #試行回数分の平均値を取る

		# }
		
	    } else {
		#試行回数を加算する
		$try_number++; 
	    }
            # 前回算出したファイルの送信周期をOLDへ
	    $dir_second_old =  $dir_second;
	}

    }

    #受信成功率算出ログを読む
    open IN, $recvRatio or die "cannot open $file ($!)";
    #printf("recvRatio_log,${recvRatio}\n");
    
    chomp(@line = <IN>);

    $segment = 1;  #受信成功率の算出回数（1から始まり、実験終了までの最大回数まで）
    foreach (@line) {

	@fields             = split(/,+/, $_);
	# 受信成功率
	$rrOneSection       = $fields[2];
	# 混雑検知しきい値と比較して、混雑解消or未解消を判定
	if ( $rrOneSection > $threashold_cong) {
	    #受信成功率算出タイミング
	    $time_cong_resolve      = $fields[0]; #混雑解消した時刻 これ以降の受信データについてカウント
	    last;
	}
	
    }

    close(IN);

     #受信成功率算出ファイルと対応する受信パケットデータログ
    $recvData_file = $ARGV[$recvData_logfiles_first+$recvRatio_fileNum];
    

    
    open IN, $recvData_file or die "cannot open $file ($!)";
    chomp(@line = <IN>);
    # printf("recvDataLog,${recvData_file}\n");
   
    # 受信パケットログからoutdateしたパケットの数とそのデータサイズを加算する
    # 第一回目のセグメントでは、outdateしたパケットを計量せず、トップ行へ戻る
    # (配列@lineはファイルの全行、$_ は１パケット分の受信ログ)

    $segment = 1;
    foreach (@line) {
	@fields              = split(/ +/, $_);

	#パケット受信時刻                                                                                                     
	$time                =  $fields[0];
	
	#受信成功率が混雑解消する時刻以降の行のみ処理する
	if ( $time >= $time_cong_resolve) {
	    #送信元IPアドレス
	    $ipaddr              = "$fields[5]";
	    #送信データサイズ
	    $datasize_segment    =  $fields[3];
	    #画像シーケンス番号
	    $img_sequence        =  $fields[7];
	    #パケットシーケンス 
	    $pac_sequence        =  $fields[9];		
	    
	    #画像シーケンス番号毎に受信したセグメント数とセグメントサイズを加算
	      
	    if (defined($hash_addr{$ipaddr})) { #ハッシュの中にip発見
		#そのシーケンス番号のパケット数を加算
		$hash_addr{$try_number}{$ipaddr}{$img_sequence}{'num_pac'}++;
		#そのパケットサイズを加算する(bytes)
		$hash_addr{$try_number}{$ipaddr}{$img_sequence}{'size_pac'}  +=$datasize_segment;
		#シーケンス番号・ラスト
		$hash_addr{$try_number}{$ipaddr}{$img_sequence}{'last_seq'}  = $img_sequence;
	    } else { #当該IPにおいてはじめての受信の場合、最初のシーケンスを保存
		$hash_addr{$try_number}{$img_sequence}{'first_pac'}          = $img_sequence;
	    }
	    
	    # printf("seg,%d,hitin,%s,image_seq, %d:%d,messnum,%d,seq_f,%d,seq_e,%d,num_outdate,%d,size,%d \n"
	    # 	   ,$segment
	    # 	   ,$ipaddr
	    # 	   ,$img_sequence
	    # 	   ,$pac_sequence
	    # 	   ,$message_num 
	    # 	   ,$hash_addr{$ipaddr}{$segment-1}{$message_num}{'seq_from'}
	    # 	   ,$hash_addr{$ipaddr}{$segment-1}{$message_num}{'seq_end'}
	    # 	   ,$hash_addr{$ipaddr}{$segment-1}{'num_outdate_pac'}
	    # 	   ,$hash_addr{$ipaddr}{$segment-1}{'size_outdate_pac'});
		    
	    
	    
	} #1つの受信ログファイルについて計算完了
    }
    close(IN);


} # END of loop


# 統計算出
$max_try = $try_number;

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

    
    my %all_recv_packet = ();

    #試行回数全体で平均を取るつもりだ
    for($try_number = 1; $try_number <= $max_try; $try_number++) {
	
	#各IPアドレス毎に統計を作る
	foreach $key_ipaddr ( sort keys %hash_addr) {
	    
	    for (my $img_sequence  = $hash_addr{$try_number}{$img_sequence}{'first_pac'}; 
		    $img_sequence <= $hash_addr{$try_number}{$ipaddr}{$img_sequence}{'last_seq'};
		    $img_sequence++ ) {

		# 画像1枚あたりの受信割合が算出できる
		my $recv_ratio_of_image = $hash_addr{$try_number}{$ipaddr}{$img_sequence}{'num_pac'}/$num_segment_of_one_image;
		
		
		for ( my $pattern, my $now_thread = $min_threash_one_image;
		      $now_thread                <= $max_threash_one_image;
		      $pattern++,
		      $now_thread                +=$adder_thread   ) {
		    # しきい値を超えたものをカウント。
		    if ($recv_ratio_of_image >= $now_thread) {
			$all_recv_packet{$ipaddr}{$pattern}{"pac"}++;
		    }

		}
	}
	#ノード合計値(パケット数、サイズ)
	$sum_num_outdate_pac  /= $num_nodes;
	$sum_size_outdate_pac /= $num_nodes;
	printf("all_num,%.3lf,all_size,%.3lf\n",
	       $sum_num_outdate_pac,$sum_size_outdate_pac);
    }
}
exit;
