#!/usr/bin/perl

# CISPログ(周期制御メッセージ送信ログ)を入力ファイルとする
# $ perl calc_control_drop.pl logfile_CISP_sentControlMess_(試行回数).log

# 受信側(画像送信ノード)のログファイルを,[各IPアドレス_(試行回数)]ディレクトリの中に
# logfile_recvCtrlMess.logとして保存する

# このプログラムでは、画像送信ノードIPをプログラム内で定めている
# 不都合があれば修正して利用すること



#受信側のログファイル名
$sender_filename = "logfile_recvCtrlMess.log";

#受信側ノードのIPオフセット,ノード数
$ip_offset = 217;
$num_nodes = 7;

#画像送信ノードIP格納
for ($node_number = 1; $node_number <= $num_nodes; $node_number++) {
    $hash_addr{$node_number}{'IPaddr'} = sprintf("192.168.50.${ip_offset}"); 
}

#実験試行回数(入力ファイル数と等しい)
$try_max      = scalar(@ARGV);
#送信したシーケンス番号の最大値を格納
$sequence_max = 0

#毎回の試行におけ;る最大シーケンス番号格納用リスト(ハッシュの初期化)
%hash_each_try = ();
%hash_logfile = ();

#各ファイルの処理
foreach $gather_node_log_file (@ARGV) { 
    
    printf("filename %s\n", $gather_node_log_file);
    #試行回数表す部分の格納(送信側ログファイル名の特定のため)
    @filename_each_field = split(/_/, $gather_node_log_file);
    $number_of_try       = $filename_each_field[4];

    #画像受信ノード側ファイルのオープン
    open GATHER_IN, $gater_node_log_file or die "cannot open $ap_file ($!)";
    chomp(@gather_node_log_line               = <GATHER_IN>);
    $lastline                                 = $#gather_node_log_line;
    @last_field                               = split(/,/, $gather_node_log_line[$lastline]);
    #シーケンス番号の最大値を格納
    $sequence_number                          = $data_each_field[4];
    $hash_each_try{$number_of_try}{'max'}     = $sequence_number;
    #送信最大シーケンス番号の格納
    if ($sequence_max < $hash_each_try{$number_of_try}{'max'}) {
	$sequence_max = $hash_each_try{$number_of_try}{'max'};
    }
    close GATHER_IN;
    
    #各ノードの受信メッセージをシーケンス番号ごとにカウントする
    for ($ip_in_hash =0, $node_number = 1; $node_number <= $num_nodes; $node_number++) {
	
	if (defined ($hash_logfile{$node_number})) { #ハッシュの中にID発見
	    $nodeIP = $hash_logfile{$node_number}; #IDに対応するIPアドレスを取得
	    $ip_in_hash = 1;
	    last;
	}
	
	
	if($ip_in_hash == 0) {
	    printf("ハッシュ該当するデータがない。なにかがおかしいよ\n");
	    exit(1); #終了
	}
	
	#画像送信ノードのログファイルをオープン
	$sender_filename_with_pass = sprintf("%s_%d/%s",
					     $nodeIP,
					     $number_of_try, 
					     $sender_filename);
	open SENDER_IN
	    , $sender_filename_with_pass or die "cannot open $sender_filename_with_pass ($!)";
	chomp(@sender_log_line = <SENDER_IN>);
	
	#画像収集ノードが各シーケンス番号のメッセージを送ったか否かをカウント
	for ($gatherNodeEachSeq=1 ; $gatherNodeEachSeq <= $hash_each_try{$number_of_try}{'maxline'}; $gatherNodeEachSeq++) {
	    $hash_logfile{$node_number}{'${sender_sequence_sender}'}{'gatherNodeSend'} += 1; 
	}
	#実際に画像送信ノードが受信したメッセージのカウント(シーケンス番号ごと)
	foreach $sender_one_line (@sender_log_line) {		
	    s/[\r\n]//g;
	    @sender_each_field    = split(/,/,$sender_one_line);
	    #シーケンス番号
	    $sender_sequence_sender = $sender_each_field[4];
	    $hash_logfile{$node_number}{'${sender_sequence_sender}'}{'senderNodeReceived'} += 1;
	    
	}
	close ST_IN;
    }
}

printf("#nodeID\tsequence\tloss_ratio\n");
#カウントした各試行における受信メッセージの統計情報を表示(パーセント表示);
for ($node_number = 1; $node_number <= $num_nodes; $node_number++) {
    
    for( $sender_sequence_sender=0; $sender_sequence_sender < $sequence_max; $sender_sequence_sender++) {
	 printf("%d\t%d\t%d\n"
		 ,$node_number
		 ,${sender_sequence_sender}
		 ,$hash_logfile{$node_number}{'${sender_sequence_sender}'}{'senderNodeReceived'});
    }
    
}





$average_delay         = $amount_delay/(scalar(@gather_node_log_line));
$average_delay_correct = $amount_delay_correct/(scalar(@gather_node_log_line));
@pass   = split(/\//,$file);

printf ("%s,average_delay,%.3f,sec,average_delay_correct,%.3f,sec\n"
	,$pass[$#pass]
	,$average_delay
	,$average_delay_correct);




exit;




