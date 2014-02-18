#!/usr/bin/perl -l
use Sys::Hostname 'hostname';

# imagesenderを繰り返し実行する。
#        使い方 : $ perl recursive_imageSender.pl (実行プログラム) (試行回数)
#        注意　試行回数はサーバ側とあわせてくださいね

# 毎回の実験結果で得られたログファイルは別名保存する


if (scalar(@ARGV)<4 || scalar(@ARGV)>4) {
    die "<Usage>: perl recursive_clientd.pl (executable program) (startPeriod) (Number of pattern) (Number of try)\n";
}


#実行プログラム名
my $exec_program       = $ARGV[0];
# 初期周期
my $startPeriod        = $ARGV[1];
# パターン数
my $number_of_pattern  = $ARGV[2];
# 試行回数
my $number_of_try      = $ARGV[3];
# 周期の上げ幅
my $period_adder       = 1;
# 実行プログラム種別(stat or adapt)
my $program_type       = "stat";

#１回の実験終了時にNICTのNTPサーバにntpdateして時刻合わせする
# 本実験では不可能？
my $ntp_server = "133.243.238.163";

#printf("Now startin experiment ${num_of_pattern} X ${ARGV[1]}\n");
#ローカルIP取得

$name = hostname;
$host = "${name}.local";

chomp($hostname);

# IPアドレスの取得
($hname,$aliases,$addrtyp,$length,@addrs) = gethostbyname($host);
#printf("host = %s\n", $hname);

# IPアドレスのドット表示
foreach $addr (@addrs) {
  $myaddr = sprintf( "%s.%s.%s.%s", unpack('CCCC', $addr));
}

$myaddr =~ s/.30./.50./;    #無線のIPに置換(XXX.XXX.50.XXX)
printf("my addr is %s. this will be a directory for saving images.",$myaddr);
my $try = 0;

# ntpdate
system ("sudo ntpdate ${ntp_server};");

#試行回数分繰り返し
my $try      = 0;
my $pattern  = 0;
my $period   = $startPeriod;
my $loop_max = $number_of_pattern*$number_of_try;

while ($try < $loop_max) {
    if ($pattern == $number_of_pattern) {
	$pattern = 0;
	$period  +=$period_adder;
    }
    printf("(${exec_program}) EXPERIMENT (period ${period}  try ${pattern} starts now (alltry ${try}/${loop_max}");
    
    system ("nohup ./${exec_program};");
    system ("killall vlc;");
    
    #ログファイル保存ディレクトリ名
    my $name_directory = sprintf("${program_type}${period}sec_try${pattern}");
    my $make_directory = sprintf("mkdir ${name_directory}");
    system (${make_directory});

    my $move_logfile = sprintf("mv *.log ${name_directory};");
    system (${move_logfile});
    my $erase_file   = sprintf("rm 192* -rf;");
    system (${erase_file});

    system ("sudo ntpdate ${ntp_server};");
    $try++;
    $pattern++;
    
}
printf("[EXPERIMENT SUCCESSFULY EXIT]\n");


