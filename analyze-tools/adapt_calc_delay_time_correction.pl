#!/usr/bin/perl

# CISPログを入力ファイルとする
# $ perl calc_delay_from_logger.pl (周期方式 static or adaptive) (基地局側のCISPログdir)/ (station側のログdir)/
# 
#    (実験前。後でもdateコマンドすると数秒単位で差が出るので、毎回の実験前にntpdateする)
#    [ファイルの名前付け規則]
#      (固定周期設定方式) 
#         * 
#         + static
#      (可変周期設定方式)
#         * adaptive_recv_data_node20_per07_a01_b01_g1_08_g2_09_reg30x.log
#          (e.x. adaptive_recv_data_node20_per07_a01_b01_g1_08_g2_09_reg30x.log)
#         + adaptive_recv_data_node(NUM OF NODES)_per(IMAGE SENDING PERIOD)_a(ALPHA VAL_b(BETA VAL)_g1_(GAMMMA1 VAL)_g2_(GAMMMA2 VAL)_reg(REGRATION PERIOD)x.log
#         (e.x. adaptive_send_node20_per07_a01_b01_g1_08_g2_09_reg30x_node192.168.10.2.log)
#
#    ファイル名規則に従わないファイルを作ったときは, ファイル名sedで置換するか
#    名付け規則（変数参照）をかえる

#    [注意 station, 基地局はntpdateで時刻同期してから実験したログでない場合、
#          delayは求められません
#          testbed04(172.23.101.2)にNTPサーバがあるので、実験前に
#
#          $ ntpdate 172.23.101.2
#
#          するとよいでしょう。
#    (実験前。後でもdateコマンドすると数秒単位で差が出るので、毎回の実験前にntpdateする)

#引数の番号
$num_arg=0;


#if ( scalar(@ARGV)<3 || scalar(@ARGV)>3 ) {
#    die "<Usage>: perl calc_delay_from_logger.pl (AP CISP log) (station send log).\nEXIT... \n";
#}

#周期送信方式
#$method = $ARGV[0];
#ファイル名(AP)
#@AP_name = $method . "_recv_data_*";
#ファイルパス(AP)
#$AP_file = $ARGV[1] . $AP_name;


$sender_filename_prefix = "adaptive_send_node";


# APファイルの一行を読み込みファイル受信時刻算出
# それに対応するファイル送信時刻をSTファイルから得る

#foreach $ap_file (@AP_file) { 
#printf("$ARGV[0], %d\n",scalar(@ARGV));



foreach $ap_file (@ARGV) { 
    #delay合計値
    $amount_delay         = 0.0;
#補正delay
    $amount_delay_correct = 0.0;
    
#delay平均値
    $average_delay         = 0.0;
    $average_delay_correct = 0.0;
#hashの初期化
    %hash_addr = ();

    open AP_IN, $ap_file or die "cannot open $ap_file ($!)";
    chomp(@ap_line = <AP_IN>);
    
    @filename_field = split(/_/, $ap_file);

    #printf("filename %s\n", $ap_file);
    foreach ( @ap_line ) {
	s/[\r\n]//g;
	@ap_field  = split(/ +/, $_);
	
	#受信時刻
	$AP_time         = $ap_field[0];
	#送信元IP
	$AP_ipaddr  = "$ap_field[5]"; #無線のIPが入る(XXX.XXX.50.XXX)
	$AP_ipaddr  =~ s/.50./.10./;    #有線のIPに置換(XXX.XXX.10.XXX)
	#シーケンス番号
	$AP_sequence     = $ap_field[7];

	#ノード数
	$AP_nodes  = $filename_field[4];
	#周期
	$AP_period = $filename_field[5];
	#ALPHA
	$AP_alpha     = $filename_field[6];
	#BETA
	$AP_beta      = $filename_field[7];
	#GAMMA1(Congestion Detect Threshold,)
	$AP_gamma1    = $filename_field[9];
	#GAMMA2(Congestion Resolv Threshold,)
	$AP_gamma2    = $filename_field[11];
	#Regulation Period
	@ap_reg_field  = split(/\./, $filename_field[12],2);
	$AP_regulation_per = $ap_reg_field[0];
	
        #対応するipaddrの送信ログファイルをオープン(受信用ファイルから読み出すように修正すること
	$st_file = sprintf("%s%s/adaptive_send_%s_%s_%s_%s_g1_%s_g2_%s_%s_node%s.log",
			   $sender_filename_prefix,
			   $AP_ipaddr,
			   $AP_nodes, 
			   $AP_period ,
			   $AP_alpha,
			   $AP_beta,
			   $AP_gamma1,
			   $AP_gamma2,
			   $AP_regulation_per,
			   $AP_ipaddr);
	open ST_IN, $st_file or die "cannot open $st_file ($!)";
	chomp(@st_line = <ST_IN>);

        # オフセットと送信開始時間から送信時刻を補正する
        # リストの探索
	
	#printf("hash lenght %d\n",scalar(%hash_addr));
	if ( scalar(%hash_addr) == 0) { #ハッシュが空
	    $ip_in_hash = 0;
	} else {
	    if (defined($hash_addr{$AP_ipaddr})) { #ハッシュの中にip発見
		$ip_in_hash = 1;
	    } else {
		$ip_in_hash = 0;
	    }
	}
	#printf("ip %s, in hash  %d\n",$AP_ipaddr, $ip_in_hash );
	if($ip_in_hash == 0) { #挿入操作
	    my @first_line = split(/ +/,$st_line[0]);
	    my $last       = $#st_line;
	    my @last_line  = split(/ +/,$st_line[$last-1]);
	    my @ntp_line   = split(/ +/,$st_line[$last]);
	    $start         = $first_line[0];
	    my $end        = $last_line[0];
	    my $ntp_offset = $ntp_line[9];
	    $hash_addr{$AP_ipaddr}{'offset'} = $offset = $ntp_offset/($end-$start); #送信端末での測定時刻1秒あたりのずれ
	    $hash_addr{$AP_ipaddr}{'start'}  = $start; #送信開始時刻(補正時刻はこの時刻を基準にする)
	    #printf("IP,$AP_ipaddr,start,$start,ntp_offset,$ntp_offset,(sec),offset,$offset,(sec)/(sec_raw)\n");
	} else { 

	    $offset = $hash_addr{$AP_ipaddr}{'offset'};
	    $start  = $hash_addr{$AP_ipaddr}{'start'};
	}
	

	foreach $one_line (@st_line) {		
	    s/[\r\n]//g;
	    @st_field    = split(/ +/,$one_line);
	    #シーケンス番号
	    $ST_sequence = $st_field[5];
	    #同シーケンス番号の検知
	    if ($AP_sequence eq  $ST_sequence) { 
		#送信時刻(送信側での時刻)
		$ST_time         = $st_field[0];
		#補正送信時刻
		$ST_time_correct = $ST_time + $offset*($ST_time-$start);
		#遅延
		$delay           = $AP_time - $ST_time;
		#補正遅延
		$delay_correct   = $AP_time - $ST_time_correct;
		#printf("ip, $AP_ipaddr,sequence,$AP_sequence,delay(raw),$delay,delay(cor),$delay_correct\n");
		$amount_delay         += $delay;
		$amount_delay_correct += $delay_correct;
	    }
	}
	close ST_IN;
    }
    $average_delay         = $amount_delay/(scalar(@ap_line));
    $average_delay_correct = $amount_delay_correct/(scalar(@ap_line));
    @pass   = split(/\//,$ap_file);
    
#    printf ("%s,average_delay,%.3f,sec,(amount_raw),%.3f,average_delay_correct,%.3f,sec,(amount_cor),%.3f\n"
#	    ,$pass[$#pass]
#	    ,$average_delay
#	    ,$amount_delay 
#	    ,$average_delay_correct
#	    ,$amount_delay_correct);
    printf ("%s,average_delay,%.3f,sec,average_delay_correct,%.3f,sec\n"
	    ,$pass[$#pass]
	    ,$average_delay
	    ,$average_delay_correct);

	    
    close AP_IN;
}

exit;




