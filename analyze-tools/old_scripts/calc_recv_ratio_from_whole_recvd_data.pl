#!/usr/bin/perl

# CISP受信パケットログデータを入力ファイル 出力は実験全体で各端末から受信した合計受信データ量（端末ごと）
# 実行例    $ perl calc_recv_ratio_from_whole_recvd_data.pl (ログファイル)


#city_cloneの１つの画像データのファイルサイズ(カメラ画像を送信する場合は変えること。)
$filesize       = 46035.0;
$number_of_sent = 600;
#１端末あたりの合計送信データ量
$amount_of_sent_data_per_node = $filesize * $number_of_sent;

#printf("%d\n",scalar(@ARGV));
if ( scalar(@ARGV)<1 || scalar(@ARGV)>1 ) {
    die "<Usage>: perl calc_recv_ratio_from_whole_recvd_data.pl (logfile_received_imagedata) .\nEXIT... \n";
}


foreach $recvd_data_file (@ARGV) { 
    #ハッシュの初期化 (ip address)('amount_data')[合計受信データ量]との組として対応付けする
    %hash_addr = ();
    
    open IN, $recvd_data_file or die "cannot open $file ($!)";
    chomp(@line = <IN>);
    #各IPアドレスごとにs合計樹陰データ量を算出
    foreach (@line) {
	@fields              = split(/ +/, $_);
	#送信元IPアドレス
	$ipaddr           = "$fields[5]";
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
	    $hash_addr{$ipaddr}{'amount_data'} = $datasize_onesegment;
	} else { 
	    $hash_addr{$ipaddr}{'amount_data'} += $datasize_onesegment;
	    
	}
	#printf("ipinhash, %d,ip %s now %d\n",$ip_in_hash,$ipaddr,$hash_addr{$ipaddr}{'amount_data'});
    }


    #各IPアドレスごとに合計受信データ量をまとめる
    if ( scalar(%hash_addr) == 0) { #ハッシュ自体が空
	printf("No data received\n");
    } else {
	foreach $key_ipaddr ( sort keys %hash_addr) {
	    $recv_ratio = $hash_addr{$key_ipaddr}{'amount_data'}/$amount_of_sent_data_per_node;
	    printf("ipaddr,%s,amount of recv,%d,amount of sent,%d,recv_ratio,%lf\n"
		   ,${key_ipaddr}
		   ,$hash_addr{$key_ipaddr}{'amount_data'}
		   ,$amount_of_sent_data_per_node
		   ,$recv_ratio);
	}
    }
    
}
