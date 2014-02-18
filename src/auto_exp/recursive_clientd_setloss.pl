#!/usr/bin/perl -l
use Sys::Hostname 'hostname';

# imagesenderを繰り返し実行する。
#        使い方 : $ perl recursive_imageSender.pl (実行プログラム) (試行回数)
#        注意　試行回数はサーバ側とあわせてくださいね

# 毎回の実験結果で得られたログファイルは別名保存する

if (scalar(@ARGV)<2 || scalar(@ARGV)>2) {
    die "<Usage>: perl recursive_clientd.pl (executable program) (Number of Try)\n";
}


#実行プログラム名
my $exec_program   = $ARGV[0];

#試行回数
my $number_of_try  = $ARGV[1];


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
#ネットワークエミュレータによるロス設定
my $loss_start     = 5;
my $loss_max       = 30;
my $loss_offset    = 5;

my $loss_now = $loss_start;
my $device_name = "wlan0";

system ("sudo ntpdate ${ntp_server};");

#試行回数分繰り返し
while ($try < $number_of_try) {
    if ($loss_now == $loss_start) { #パケットロス率設定
	system ("sudo tc qdisc add dev ${device_name} root netem loss ${loss_now}%;");
    } else {
	system ("sudo tc qdisc change dev ${device_name} root netem loss ${loss_now}%;");
    }
    printf("[Emulator set] %d percent loss\n",${loss_now});
    printf("(clientd) EXPERIMENT number %d starts now",$try);
    
    system ("./${exec_program};");
    system ("killall vlc;");
    
    my $make_logfile = sprintf("mv logfile_imageSender_sentData.log clientd_sentImageData_try%d.log;"
			       ,${try});
    system (${make_logfile});
    my $make_logfile = sprintf("mv  logfile_recvCtrlMess.log clientd_recvCtrlMess_%d.log;"
			       ,${try});
    system (${make_logfile});
    my $make_logfile = sprintf("mv  logfile_imageSender_sentValMess.log clientd_sentAmntDataMess_%d.log;"
			       ,${try});
    system (${make_logfile});
    my $make_picture = sprintf("mv ${myaddr}  ${myaddr}_${try};");
    system (${make_picture});

    system ("sudo ntpdate ${ntp_server};");
    $try++;
    $loss_now += $loss_offset;

}
system ("sudo tc qdisc del dev ${device_name} root netem loss ${loss_max}%;");
printf("[EXPERIMENT SUCCESSFULY EXIT]");


