if ($time_first == -1) { #まず、最初の行の時刻を区間最初の時刻として使用
	    $time_first = $SERVER_time;

	} elsif ($SERVER_time - $time_first >= $timer_calc) { #平均遅延算出
	    #すべての送信元それぞれで遅延を算出
	    printf("calcnum,${segment},");
	    foreach $key_ipaddr ( sort keys %hash_addr) {
		if($hash_addr{$key_ipaddr}{'num_packet'} > 0) {
		    $hash_addr{$key_ipaddr}{$segment}{'ave_delay'} 
		    = $hash_addr{$key_ipaddr}{'per_packet_delay_sum'}/$hash_addr{$key_ipaddr}{'num_packet'};
		    printf("ip,${key_ipaddr},numpac,$hash_addr{$key_ipaddr}{'num_packet'},delaysum,$hash_addr{$key_ipaddr}{'per_packet_delay_sum'},delay,${hash_addr{$key_ipaddr}{$segment}{'ave_delay'}}\n");
		}
		
		#初期化
		$hash_addr{$key_ipaddr}{'per_packet_delay_sum'} = 0;
		$hash_addr{$key_ipaddr}{'num_packet'}           = 0;
	    }
	    $segment++;
	    if($exp_type eq "adapt") { 	# 可変周期は算出間隔更新
		$timer_calc = $timerOneSection{$segment};

	    }             #固定周期(stat)の場合は算出間隔固定値

	    #算出が終わったらserver timeを新たに区間最初の時刻にする
	    $time_first = $SERVER_time;
	}

	#送信周期
#	@server_period  = split(/\./, $filename_field[5],2); #
#	$SERVER_period  = $server_period[0];
	
---file. /home/matsuday/syuron_experiment/image_trans/singlehop/Static_log,delayCalculation---
1,ip,192.168.30.217,delay,20.0179702691706,ip,192.168.30.218,delay,3.20249305471489,ip,192.168.30.219,delay,22.3890876222924,ip,192.168.30.220,delay,24.6931639499157,ip,192.168.30.221,delay,24.985091973966,ip,192.168.30.222,delay,84.4870898009485,ip,192.168.30.223,delay,13.9538683130233,ip,192.168.30.224,delay,25.517663583189,ip,192.168.30.225,delay,26.987363939823,ip,192.168.30.226,delay,12.8972553736308,all,25.9131047880674
2,ip,192.168.30.217,delay,15.1147797050704,ip,192.168.30.218,delay,4.39202642209052,ip,192.168.30.219,delay,22.6718382436007,ip,192.168.30.220,delay,16.0732481986778,ip,192.168.30.221,delay,18.0093697879182,ip,192.168.30.222,delay,38.8037599184934,ip,192.168.30.223,delay,13.9793148125292,ip,192.168.30.224,delay,21.8314848834229,ip,192.168.30.225,delay,27.198113736709,ip,192.168.30.226,delay,18.0800194939935,all,19.6153955202506
3,ip,192.168.30.217,delay,21.458300454916,ip,192.168.30.218,delay,4.738813102959,ip,192.168.30.219,delay,27.163070538066,ip,192.168.30.220,delay,21.0043747437513,ip,192.168.30.221,delay,20.4620568349852,ip,192.168.30.222,delay,98.4289529994036,ip,192.168.30.223,delay,14.5972692693983,ip,192.168.30.224,delay,10.9771977426037,ip,192.168.30.225,delay,40.917950459456,ip,192.168.30.226,delay,5.35833540501262,all,26.5106321550552
4,ip,192.168.30.217,delay,25.4051586483415,ip,192.168.30.218,delay,2.87954076548985,ip,192.168.30.219,delay,34.5628723595432,ip,192.168.30.220,delay,19.6201760246136,ip,192.168.30.221,delay,19.1317167350317,ip,192.168.30.222,delay,86.3830778593702,ip,192.168.30.223,delay,14.8855765338703,ip,192.168.30.224,delay,20.0146267108866,ip,192.168.30.225,delay,24.8305805760521,ip,192.168.30.226,delay,13.4227713491122,all,26.1136097562311
5,ip,192.168.30.217,delay,,ip,192.168.30.218,delay,,ip,192.168.30.219,delay,,ip,192.168.30.220,delay,,ip,192.168.30.221,delay,,ip,192.168.30.222,delay,,ip,192.168.30.223,delay,,ip,192.168.30.224,delay,,ip,192.168.30.225,delay,,ip,192.168.30.226,delay,,all,0
delay_average_all_segments,19.6305484439209
