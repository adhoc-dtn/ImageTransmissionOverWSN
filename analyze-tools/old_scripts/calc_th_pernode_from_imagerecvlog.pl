#!/usr/bin/perl

# サーバ側ログファイル（受信データ）を入力ファイルとする

# 各ノードで1秒ごとの
# ・合計受信スループットを全区間平均して出力 単位はkbit/sec

# 試行回数

if ( scalar(@ARGV)<1 || scalar(@ARGV)>1 ) {
    die "<Usage>: perl calc_th_pernode_from_imagerecvlog.pl (logfile_received_imagedata) .\nEXIT... \n";
}

foreach $file (@ARGV) {
    #ハッシュの初期化 
    # (ip address)('time_first_recv') 初回データ受信時刻
    # (ip address)('time_last_recv')  最後のデータの受信時刻
    # (ip address)('amount_data')     合計受信データ量
    %hash_addr = ();

    open IN, $file or die "cannot open $file ($!)";
    chomp(@line = <IN>);
    
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
	
	if($ip_in_hash == 0) { #ハッシュへの項目追加
	    $hash_addr{$ipaddr}{'time_first_recv'} = $time;
	    $hash_addr{$ipaddr}{'amount_data'}     = $datasize_onesegment;
	    
	} else { 
	    $hash_addr{$ipaddr}{'time_last_recv'}  = $time;
	    $hash_addr{$ipaddr}{'amount_data'}    += $datasize_onesegment;
	    
	}
	#printf("ipinhash, %d,ip %s,first,%llf,last,%llf,amount,%d\n"
	 #      ,$ip_in_hash
	  #     ,$ipaddr
	   #    ,$hash_addr{$ipaddr}{'time_first_recv'}
	    #   ,$hash_addr{$ipaddr}{'time_last_recv'}
	     #  ,$hash_addr{$ipaddr}{'amount_data'});
    }
    
    #各IPアドレスごとに合計受信データ量をまとめる
    if ( scalar(%hash_addr) == 0) { #ハッシュ自体が空
	printf("No data received\n");
    } else {
	foreach $key_ipaddr ( sort keys %hash_addr) {
	    $throughput 
		= $hash_addr{$key_ipaddr}{'amount_data'}*8
	    /(1000*($hash_addr{$key_ipaddr}{'time_last_recv'} - $hash_addr{$key_ipaddr}{'time_first_recv'}));

	    printf("ipaddr,%s,throughput,%lf,kbps,amount,%d,first,%lf,last,%lf\n"
		   ,${key_ipaddr}
		   ,$throughput
		   ,$hash_addr{$key_ipaddr}{'amount_data'}
		   ,$hash_addr{$key_ipaddr}{'time_first_recv'}
		   ,$hash_addr{$key_ipaddr}{'time_last_recv'})
	}
    }
}

exit;
