#!/usr/bin/perl

# 試行全体の受信成功率算出(全体、ノード別それぞれ)用スクリプトファイル
# [実行例]
# $ perl calc_average_rr.pl (受信成功率算出ログファイル)
# 
# 受信成功率算出用ログは以下のような形式にしがたう

#(以下、\部分は実際には改行が入らない。次の行を含めて１行となる)
#  時刻[0],全体受信成功率 [1,2],       送信トータル[3,4],受信トータル[5,6] \
# ,現在設定されている受信成功率[7,8], CD or CR[9 - 12], ipX受信成功率など[13-20]8フィールド
# 1392597130.366850,recv_r,0.654071,total_send(message),2100000,total_recv,1373550,\
# frame_sending_period,36.000000,CD,1,CR,0,(ip),192.168.50.217,(rr),0.654071,send,2100000,recv,1373550,

use utf8;
use open ":utf8";
binmode STDIN, ':encoding(cp932)';
binmode STDOUT, ':encoding(cp932)';
binmode STDERR, ':encoding(cp932)';

#ノード毎の受信成功率が含まれているフィールドまでのフィールド数
$num_field_till_node_log = 13;
#ノード別データが含まれているフィールド数
$num_field_one_node      = 8;
#ノード別データが含まれているフィールドにおけるipアドレス・受信成功率のフィールド部分
$padding_ip          = 1;
$padding_recv_ratio  = 3;

#ハッシュの初期化
%hash_addr = ();
#試行回数の初期化
$try_number = 0;


#ノード毎に受信成功率を算出する
foreach $server_file (@ARGV) { 

    open SERVER_IN, $server_file or die "cannot open $server_file ($!)";
    chomp(@server_line = <SERVER_IN>);
    
    printf("now processing filename %s\n", $server_file);

    $line_number    = 0;
    
    #１行ずつ処理する
    foreach $one_line (@server_line) { 
	my @fields          = split(/,+/, $one_line);
	#printf("[read line] @{fields}\n");
	$hash_addr{"whole"}{"recv_ratio"} {$line_number} += $fields[2]; #全体の受信成功率
	$hash_addr{"whole"}{"send_period"}{$line_number} += $fields[8]; #現在設定されている転送周期

	#各ノードのフィールドから受信成功率の統計を取る
	for(my $field_num = $num_field_till_node_log;
	    $field_num < $#fields;
	    $field_num+=$num_field_one_node) {

	    # IPアドレス
	    my   $ipaddr     = $fields[$field_num + $padding_ip];
	    #受信成功率
	    my   $recv_ratio = $fields[$field_num + $padding_recv_ratio];
	    # そのIPアドレスにおける受信成功率
	    $hash_addr{$ipaddr}{"recv_ratio"}{$line_number} += $recv_ratio;
	    # printf("%s,%.3lf\n",$ipaddr,${hash_addr{$ipaddr}{"recv_ratio"}{$line_number}});
	    
	}
	$line_number++;
    }
    #行数最大値
    $max_line_number = $line_number;
    #試行回数
    $try_number++;
    close SERVER_IN; #サーバログ１つ分の終了
} #全ログファイルについてデータをハッシュ内に挿入完了

#試行回数全体での受信統計をとる
my $max_try = $try_number;

for (my $line_number = 0; $line_number < $max_line_number ; $line_number++) {

    #試行回数で除算
    $hash_addr{"whole"}{"recv_ratio"} {$line_number} /= $max_try;
    $hash_addr{"whole"}{"send_period"}{$line_number} /= $max_try; 
    
    printf("seg,%d,sending_periog,%.3lf,whole_rr,%.3lf,",
	   $line,
	   $hash_addr{"whole"}{"send_period"}{$line_numbe},
	   $hash_addr{"whole"}{"recv_ratio"} {$line_number}
	);
    foreach $key_ipaddr ( sort keys %hash_addr) {
	if( $key_ipaddr eq "whole" ) {
	    last;
	}
	#ノードの受信成功率
	$hash_addr{$key_ipaddr}{"recv_ratio"}{$line_number}/= $max_try;
	printf("ip,%s,node_rr,%.3lf,"
	       ,$key_ipaddr
	       ,$hash_addr{$key_ipaddr}{"recv_ratio"}{$line_number});
    }
    
}



exit;




