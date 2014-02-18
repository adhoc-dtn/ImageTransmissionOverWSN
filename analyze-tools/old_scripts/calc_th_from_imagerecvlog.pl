#!/usr/bin/perl

# サーバ側ログファイル（受信データ）を入力ファイルとする

# 1秒ごとの
# ・合計受信スループットを全区間平均して出力 単位はkbit/sec

# 試行回数

foreach $file (@ARGV) {
        open IN, $file or die "cannot open $file ($!)";

        chomp(@line = <IN>);
	
	$amount = 0.0;
	$average_throughput = 0.0;
	@first_field  = split(/ +/, $line[0]);
	$start        = $first_field[0];
        #printf("start == $start\n");
	$lastline     = $#line;
	@last_field   = split(/ +/, $line[$lastline]);
	$end          = $last_field[0];
	#printf("last == $end\n");
	
        foreach (@line)
        {		
	    s/[\r\n]//g;
	    @field  = split(/ +/,$_);
	    $amount += int($field[3]);
	}
	close IN;
	#平均受信スループット算出
	
	$average_throughput = $amount/($end-$start)*8.0/1000.0;
	#printf("time == %lf, amount == %lf\n",$end-$start,$amount);
	#printf("on paket size must be %d (%d/%d)\n",$amount/($lastline+1),$amount,$lastline);
	@pass   = split(/\//,$file);

	printf ("%s,num_packet,%d,amountOfData,%d,throughput,%.3f,kbps\n"
		,$pass[$#pass]
		,$lastline
		,$amount
		,$average_throughput);

        
	
}

exit;

