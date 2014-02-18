#!/usr/bin/perl

# CISPログを入力ファイルとする
# $ perl calc_delay_from_logger.pl (周期方式 static or adaptive) (基地局側のCISPログdir)/ (station側のログdir)/
# 
#    [ファイルの名前付け規則]
#         + static(adaptive)_recv(send)_data_(nodeX.X.X.X).log
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


if ( scalar(@ARGV)<3 || scalar(@ARGV)>3 ) {
    die "<Usage>: perl calc_delay_from_logger.pl (AP CISP log) (station send log).\nEXIT... \n";
}

#周期送信方式
$method = $ARGV[0];
#ファイル名(AP)
$AP_name = $method . "_recv_data_*";
#ファイルパス(AP)
$AP_file = $ARGV[1] . $AP_name;

#delay合計値
$amount_delay  = 0.0;
#delay平均値
$average_delay = 0.0;

# APファイルの一行を読み込みファイル受信時刻算出
# それに対応するファイル送信時刻をSTファイルから得る

foreach $ap_file (@AP_file) { 
    @filename_field = = split(/_+/, $ap_file);
    open AP_IN, $ap_file or die "cannot open $file ($!)";
    chomp(@ap_line = <AP_IN>);

    foreach ( @ap_line ) {
	s/[\r\n]//g;
	@ap_field  = split(/ +/, $_);
	
	#受信時刻
	$AP_time     = $ap_field[0];
	#送信元IP
	$AP_ipaddr   = $ap_field[5];
	#シーケンス番号
	$AP_sequence = $ap_field[7];
	
	#対応するipaddrの送信ログファイルをオープン
	$st_file = $ARGV[2] . $method . "_send_" . $filename_field[3] . "_" . $filename_field[4] . "node" . $AP_ipaddr;
	open ST_IN, $st_file or die "cannot open $file ($!)";
	chomp(@st_line = <ST_IN>);

	foreach (@st_line) {		
	    s/[\r\n]//g;
	    @st_field    = split(/ +/,$_);
	    #シーケンス番号
	    $ST_sequence = $st_field[5];
	    #同シーケンス番号の検知
	    if ($AP_sequence eq  $ST_sequence) { 
		#送信時刻
		$ST_time = $st_field[0];
		#遅延
		$delay = $AP_time - $ST_time;
		$amount_delay += $delay;
	    }
	}
	close ST_IN;
    }
    $average_delay = $amount_delay/(scalar(ap_line));
    @pass   = split(/\//,$file);
    
    printf ("%s,average_delay,%.3f,sec\n"
	    ,$pass[$#pass]
	    ,$average_delay);
    
    close AP_IN;
}
