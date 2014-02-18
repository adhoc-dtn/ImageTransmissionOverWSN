#!/usr/bin/perl -l

# regulation periodにおける試行回数を指定し、CISPを実行する。
#        使い方 : $ perl recursive_CISP.pl (実行プログラム) (通信デバイス名) (広告周期) (regulation period) (試行回数)

# 毎回の実験結果で得られたログファイルは別名保存する

if (scalar(@ARGV)<5 || scalar(@ARGV)>5) {
    die "<Usage>: perl recursive_serverd.pl (executable program) (device for communication) (initial data sending period) (regulation period) (Number of Try)";
}
my $exec_program         = $ARGV[0];
my $device               = $ARGV[1];
my $data_sending_period  = $ARGV[2];
my $regulation_period    = $ARGV[3];
my $number_of_try        = $ARGV[4];

my $try = 0;
#NICTのNTPサーバにntpdateして時刻合わせする
my $ntp_server = "133.243.238.163";
my $offsetIP   = 217;
my $node_max = 7;
#毎回の試行で広告周期を変える場合、数値を設定、変えなければ0に
my $period_offset = 0.0;

#ネットワークエミュレータによるロス設定
my $loss_start     = 5;
my $loss_max       = 30;
my $loss_offset    = 5;

my $loss_now = $loss_start;
my $device_name = "wlan0";

#クライアントプログラムの実行後に実行
system ("sudo ntpdate ${ntp_server};");
sleep 10;

while ($try < $number_of_try) {

    if ($loss_now == $loss_start) { #パケットロス率設定
	system ("sudo tc qdisc add dev ${device_name} root netem loss ${loss_now}%;");
    } else {
	system ("sudo tc qdisc change dev ${device_name} root netem loss ${loss_now}%;");
    }
    printf("[Emulator set] %d percent loss\n",${loss_now});
    
    printf("\n[(serverd) EXPERIMENT number %d starts now]********",$try);
    
    system ("./${exec_program} ${device} ${data_sending_period} ${regulation_period};");
    my $make_logfile = sprintf("mv logfile_CISP_calcRecvRatio.log  serverd_calcRecvRatio_try%d.log"
			       ,${try});
    system (${make_logfile});
    my $make_logfile = sprintf("mv logfile_CISP_recvImageData.log serverd_recvImageData.log_%d.log"
			       ,${try});
    system (${make_logfile});
    my $make_logfile = sprintf("mv logfile_CISP_recvValNotifyMess.log serverd_recvAmntDataMess_%d.log"
			       ,${try});
    system (${make_logfile});
    my $make_logfile = sprintf("mv logfile_CISP_sentControlMess.log serverd_sentCntlMess_%d.log"
			       ,${try});
    system (${make_logfile});
    
    
    my $node     = 0;
    while ($node < $node_max ) {
	my $addr = $offsetIP+$node;
	my $make_picture = sprintf("mv 192.168.50.%d 192.158.50.%d_%d"
			       ,${addr},${addr},${try});
	system (${make_picture});
	$node++;
    }

    system ("sudo ntpdate ${ntp_server};");
    $try++;
    $loss_now += $loss_offset;
    
    $data_sending_period += $period_offset;
#imageSender(クライアント)がビーコン待ち状態になるまで待ち状態にするべき 今のところ適当にスリープしている
    sleep 10; 
}
system ("sudo tc qdisc del dev ${device_name} root netem loss ${loss_max}%;");


printf("[EXPERIMENT SUCCESSFULY EXIT]");
