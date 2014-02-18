#!/usr/bin/perl -l

# regulation periodにおける試行回数を指定し、CISPを実行する。
#        使い方 : $ perl recursive_CISP.pl (実行プログラム) (通信デバイス名) (広告周期) (regulation period) (試行回数)

# 毎回の実験結果で得られたログファイルは別名保存する

if (scalar(@ARGV)<6 || scalar(@ARGV)>6) {
    die "<Usage>: perl recursive_serverd.pl (executable program) (device for communication) (initial data sending period) (regulation period) (Number of pattern) (Number of Try)";
}
my $exec_program         = $ARGV[0];
my $device               = $ARGV[1];
my $startPeriod          = $ARGV[2];
my $regulation_period    = $ARGV[3];
my $number_of_pattern    = $ARGV[4];
my $number_of_try        = $ARGV[5];

#毎回の試行で広告周期を変える場合、数値を設定、変えなければ0に
my $period_adder = 2.0;
# 実行プログラム種別(stat or adapt)
my $program_type       = "stat";


#NICTのNTPサーバにntpdateして時刻合わせする
my $ntp_server = "133.243.238.163";
# my $offsetIP   = 217;		
# my $node_max = 15;

#ntpdate時刻合わせ(遅延測定のため)
system ("sudo ntpdate ${ntp_server};");
#クライアントプログラムの実行後に実行
sleep 10;

#試行回数分繰り返し
my $try      = 0;
my $pattern  = 0;
my $period   = $startPeriod;
my $loop_max = $number_of_pattern*$number_of_try;

while ($try < $loop_max) {
    if ($pattern == $number_of_pattern) {
	$pattern = 0;
	$period  += $period_adder;
    }
    printf("(${exec_program}) EXPERIMENT (period ${period}  try ${pattern} starts now (alltry ${try}/${loop_max}");
    system ("nohup ./${exec_program} ${device} ${period} ${regulation_period};");

    #ログファイル保存ディレクトリ名
    my $name_directory = sprintf("${program_type}${period}sec_try${pattern}");
    my $make_directory = sprintf("mkdir ${name_directory}");
    system (${make_directory});

    my $move_logfile = sprintf("mv *.log ${name_directory};");
    system (${move_logfile});
    my $erase_file   = sprintf("rm 192* -rf;");
    system (${erase_file});
    
    
    # my $node     = 0;
    # while ($node < $node_max ) {
    # 	my $addr = $offsetIP+$node;
    # 	my $make_picture = sprintf("mv 192.168.50.%d 192.158.50.%d_%d"
    # 			       ,${addr},${addr},${try});
    # 	system (${make_picture});
    # 	$node++;
    # }

    system ("sudo ntpdate ${ntp_server};");
    $try++;
    $pattern++;
    
#imageSender(クライアント)がビーコン待ち状態になるまで待ち状態にするべき 今のところ適当にスリープしている
    sleep 10; 
}
printf("[EXPERIMENT SUCCESSFULY EXIT]\n");

