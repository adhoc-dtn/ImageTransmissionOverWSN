#!/usr/bin/perl

# サーバ側ログファイル（受信データ）を入力ファイルとする

# 各ノードで1秒ごとの
# ・合計受信スループットを全区間平均して出力 単位はkbit/sec

# 試行回数

if ( scalar(@ARGV)<1 || scalar(@ARGV)>1 ) {
    die "<Usage>: perl calc_th_pernode_from_imagerecvlog.pl (logfile_received_imagedata) .\nEXIT... \n";
}
#何秒間の平均スループットを取るかというパラメータ
$one_segment = 100;

#入力ファイル数はワイルドカードなどで何個でも指定可能とする


foreach $file (@ARGV) {
    #ハッシュの初期化 
    # (ip address)('time_first_recv') 初回データ受信時刻
    # (ip address)('time_last_recv')  最後のデータの受信時刻
    # (ip address)('amount_data')     合計受信データ量
    %hash_addr = ();

    open IN, $file or die "cannot open $file ($!)";
    chomp(@line = <IN>);
    
    # $isTimeFirst = 1;
    # $first = 0;

    foreach (@line) {
	@fields              = split(/ +/, $_);
	#送信元IPアドレス
	$ipaddr              = "$fields[5]";
	#受信時刻
	$time                =  $fields[0];
	#送信データサイズ
	$datasize_onesegment =  $fields[3];


	#ハッシュから該当IPアドレスの項目の有無を調べる
	if ( scalar(%hash_addr) == 0) { #ハッシュ自体が空
	    $ip_in_hash = 0;
	} else {
	    if (defined($hash_addr{$ipaddr})) { #ハッシュの中にip発見
		$ip_in_hash = 1;
	    } else { #ハッシュの中に該当ipなし
		$ip_in_hash = 0;
	    }
	}
	
	if($ip_in_hash == 0  ) { #ハッシュへの項目追加
	    $hash_addr{$ipaddr}{'time_first'}         = $time;
	    $hash_addr{$ipaddr}{'data_size_one_sec'}  = $datasize_onesegment;
	    $hash_addr{$ipaddr}{'segment'}            = 1; #時間軸１秒目

	} else { 
	    if($time - $hash_addr{$ipaddr}{'time_first'} >= $one_segment) {

		$hash_addr{$ipaddr}{'th'}{$hash_addr{$ipaddr}{'segment'}} 
		= $hash_addr{$ipaddr}{'data_size_one_sec'}; #1秒の合計受信データ量
		# printf("ip %s,segment %d, th,%d,bytes/sec\n"
		#        ,$ipaddr
		#        ,$hash_addr{$ipaddr}{'segment'}
		#        ,$hash_addr{$ipaddr}{'th'} {$hash_addr{$ipaddr}{'segment'}} );
		$hash_addr{$ipaddr}{'segment'}++;
		$hash_addr{$ipaddr}{'time_first'}         = $time;
		$hash_addr{$ipaddr}{'data_size_one_sec'}  = 0; #初期化
		
	    }
	    $hash_addr{$ipaddr}{'data_size_one_sec'}  += $datasize_onesegment;
	}
	# printf("ip %s,segment %d, amount,%d\n"
	#        ,$ipaddr
	#        ,$hash_addr{$ipaddr}{'segment'}
	#        ,$hash_addr{$ipaddr}{'data_size_one_sec'});
    }
    
    #各IPアドレスごとに合計受信データ量をまとめる
    if ( scalar(%hash_addr) == 0) { #ハッシュ自体が空
	printf("No data received\n");
    } else {
	#最大セグメント数を算出
	$max_segment = 0;
	foreach $key_ipaddr ( sort keys %hash_addr) {
	    printf("%s,",${key_ipaddr});
	    if( $max_segment < $hash_addr{$ipaddr}{'segment'}) {
		$max_segment = $hash_addr{$ipaddr}{'segment'};
	    }
	}
	printf("\n");
	#ノードIP毎に各秒での受信スループットを算出
	for($segment = 1; $segment <= $max_segment; $segment++) {
	    printf("%d,",$segment);
	    foreach $key_ipaddr ( sort keys %hash_addr) {
	    #printf("%s,",${key_ipaddr});
	    
		printf("%.3lf,kbps,",
		       $hash_addr{$key_ipaddr}{'th'}{$segment}*8/($one_segment*1000) );
	    }
	    printf("\n");
	}
    }
}

exit;
