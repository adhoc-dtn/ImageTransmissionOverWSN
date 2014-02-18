#!/usr/bin/perl

# 隣接ノードアドレス(１０進に戻したもの)を返す。
# (元々の経路表の次ホップが重複している場合でも、次ホップは重複なしで表示する)
# (通信に用いるインタフェースを$target_deviceに設定) デフォルトwlan0

# 使用例
#     $ ./get_next_node.pl (wirels)(-c or -p) (IPアドレス：十進表記のIPv4アドレス)
#              -c ( childlen) 指定時、IPアドレスを除く次ホップノードをすべて表示する
#              -p ( parent  ) 指定時、IPアドレスにパケットを送信する際のGWノードを表示する
#

# [注意] 経路表の作り方------------------------------------
#
# (i) 2hop以上の宛先に対しては特に注意はない 普通に作れば良い
#     (以下、ネットワークアドレスは任意で可 例として192.168.50.0/24を挙げる) 
#     dest 192.168.50.YYY gw 192.168.50.XXX 
#
# (ii)1hop隣接ノード(これ注意)
#     まず
#     dest 192.168.50.0  gw *
#     を削除する。(しなくてもOK。ただし次ホップは自ネットワークの経路表が存在する状況で
#     １ホップ隣接ノードも存在させるのは表記的によくない)
#     # route del -net 192.168.50.0 netmask 255.255.255.0;
#     次に隣接ノードアドレスに対して、経路表を作成する
#     # route add 192.168.50.10 dev wlan0
#     結果として
#
#     受信先サイト    ゲートウェイ    ネットマスク   フラグ Metric Ref 使用数 インタフェース
#     192.168.50.10   *               255.255.255.255 UH    0      0        0 wlan0
#     のようになればOK(受信先サイトの中にネットワークアドレスを含めず、ホストのみにする)
#-----------------------------------------------------------

#通信デバイス名
#my $target_device = "eth0";

#経路表 Ubuntu12.04ではここ
#(違うLinux distributionなら適切なファイルに変える)
my $file = "/proc/net/route";; 

open(my $fh, "<", $file)
  or die "Cannot open $file: $!";


my $usage_str = "<Usage>: perl ./get_next_hop.pl (interface name: e.g. wlan0 ) (-c or -p) (IPv4 address).\n";
my $TPar = "-p";
my $TChi = "-c";

if (scalar(@ARGV) == 3) {
    #printf("You set option %s IP as %s\n",$ARGV[0], $ARGV[1]);    
    $target_device = $ARGV[0];
    if ($ARGV[1] eq  $TChi){
	$nodeType  = $TChi;
	$filterIP  = $ARGV[2];
    } elsif ($ARGV[1] eq $TPar) {
	$nodeType = $TPar;
	$destIP   = $ARGV[2];
    } else {
	die $usage_str;
    }
}else {
    die $usage_str;
}

my $line_number = 0;
%hash_decaddr = ();

while (my $line = readline $fh) { 
    if ($line_number > 0) {
        # chompで、改行を取り除く
	chomp $line;	
	@each_field   = split(/\t+/,$line);
        my $device    = $each_field[0];
	my $dest_node = $each_field[1];
	my $gw_node   = $each_field[2];

	if ($device eq $target_device) { #通信用デバイス
	    #printf("nodeType %s\n",$nodeType);
	    if($nodeType eq $TChi ) { #子ノードを表示する
		
		# 宛先がフィルタIPであるときの１ホップ隣接ノードもフィルタの必要あり
		# 本来リスト化するべき
		if (convert_hexaddr_to_decaddr($dest_node) eq $filterIP) {
		    $node_to_root = $gw_node;
		    
		}

		if((convert_hexaddr_to_decaddr($dest_node) ne $filterIP)
		   and (convert_hexaddr_to_decaddr($gw_node)ne $filterIP) ){
		    if ($gw_node ne "00000000") { #2ホップ隣接ノード gw にホスト名が指定されている
			insert_decaddr_into_hash($gw_node);
		    } else { #1ホップ隣接ノード (gw 00000000は次ホップが自ネットワーク)
			insert_decaddr_into_hash($dest_node);
		    }
		}
	    } elsif($nodeType eq $TPar ){ #1hop親ノードを表示する
		
		if ( $destIP eq convert_hexaddr_to_decaddr($dest_node)) { 
		    #printf("next hop %s will be inserted\n",$next_hop);
		    if ($gw_node ne "00000000") { #宛先ノードが2ホップ以上先のノードである場合
			insert_decaddr_into_hash($gw_node);
		    } else {
		    	insert_str_into_hash($destIP); #1ホップ隣接である場合
		    }
		}
	    }
	}

    }
    $line_number++;
}

if($nodeType eq $TChi ) {
# ルート方向への次ホップをhashから削除する 
# 本来リスト化する必要ありだが、ルート方向への次ホップは１つと仮定している
    del_deccaddr_in_hash($node_to_root);
}


#ハッシュに格納された次ホップを表示
if ( scalar(%hash_decaddr) == 0) { #ハッシュ自体が空
#	printf("No data received\n");
}else {
    foreach $key_ipaddr ( sort keys %hash_decaddr) {
	if(scalar ($hash_decaddr{$key_ipaddr}{'next_hop'}) > 0) {
	    printf("%s\n",$hash_decaddr{$key_ipaddr}{'next_hop'});
	}
    }
}

close $fh;

#---------------------------------------------------------------------------
# convert_hexaddr_to_decaddr( hex_addr )
# 機能  : リトルエンディアンで表記されたipv4アドレスを10進ドット文字列で返す
# 引数  ; hex_addr 16進(リトリエンディアン)で表記されたipv4アドレス(文字列)
#----------------------------------------------------------------------------
sub convert_hexaddr_to_decaddr {
    my @inbound_param = @_;
    my $hex_addr = $inbound_param[0];
    my $ipv4_num_halfbytes = 8;
    my @stack        = ();
   
    my $decaddr;   
    #スタックにプッシュ
    $one_bytes = substr ($hex_addr, 0, 2);
    if ( hex($one_bytes) == 0) {
	return;
    } else {
	push (@stack, $one_bytes);
    }
	
    for ($halfbytes = 2; $halfbytes < $ipv4_num_halfbytes; $halfbytes+=2) {
	$one_bytes = substr ($hex_addr, $halfbytes, 2);
	push (@stack, $one_bytes);
    }
 
    #スタック内ポップ

    $decaddr = sprintf("%d.%d.%d.%d", hex(pop @stack),hex(pop @stack),hex(pop @stack),hex(pop @stack));
    return $decaddr;
     
}



#---------------------------------------------------------------------------
# insert_decaddr_into_hash( hex_addr )
# 機能  : リトルエンディアンで表記されたipv4アドレスを10進ドット記法でハッシュ
#         (%hash_decaddr)に挿入 重複したものは挿入しない
# 引数  ; hex_addr 16進(リトリエンディアン)で表記されたipv4アドレス(文字列)
#----------------------------------------------------------------------------
sub insert_decaddr_into_hash {
    my @inbound_param = @_;
    my $decaddr = convert_hexaddr_to_decaddr($inbound_param[0]);

    #ハッシュに挿入
    if (!defined($hash_decaddr{$decaddr})) { 
	$hash_decaddr{$decaddr}{'next_hop'} = $decaddr;
	#printf("%s",$decaddr);
    }
     
}

#---------------------------------------------------------------------------
# insert_decaddr_into_hash( hex_addr )
# 機能  : リトルエンディアンで表記されたipv4アドレスを10進ドット記法でハッシュ
#         (%hash_decaddr)に挿入 重複したものは挿入しない
# 引数  ; hex_addr 16進(リトリエンディアン)で表記されたipv4アドレス(文字列)
#----------------------------------------------------------------------------
sub del_deccaddr_in_hash {
    my @inbound_param = @_;
    my $decaddr = convert_hexaddr_to_decaddr($inbound_param[0]);
    # 削除
    $delip = delete $hash_decaddr{$decaddr}{'next_hop'};
#    printf("suc to del $delip\n");
     
}
#---------------------------------------------------------------------------
# insert_str_into_hash( ipadddr )
# 機能  : ドット表記されたipv4アドレスを挿入 重複したものは挿入しない
# 引数  ;  16進(リトリエンディアン)で表記されたipv4アドレス(文字列)
#----------------------------------------------------------------------------
sub insert_str_into_hash {
    my @inbound_param = @_;
    my $decaddr = $inbound_param[0];

    #ハッシュに挿入
    if (!defined($hash_decaddr{$decaddr})) { 
	$hash_decaddr{$decaddr}{'next_hop'} = $decaddr;
	#printf("%s",$decaddr);
    }
     
}
