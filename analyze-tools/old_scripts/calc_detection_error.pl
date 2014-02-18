#!/usr/bin/perl

# サーバ側ログ（受信成功率データ）を入力ファイルとする

# 複層検知の誤制御情報を出力する

# 試行回数

foreach $file (@ARGV) {
        open IN, $file or die "cannot open $file ($!)";

        chomp(@line = <IN>);
	
	$amount = 0.0; #congestion detectionの項目
	$num_of_line = 0.0;
	$ratio_detecttion_error = 0.0;
	
        foreach (@line)
        {		
	    s/[\r\n]//g;
	    @field  = split(/,+/,$_);
	    #printf("field[2] == %.3f,field[10] == %.3f\n"
	#	   ,$field[2]
	#	   ,$field[10]);
	    #recv_ratioが0になると終了する
	    last if ($field[2] == 0.0);
	    $amount += int($field[10]);
	    $num_of_line++;
	}
	close IN;
	#誤制御率の算出
	
	$ratio_detecttion_error = $amount/$num_of_line;
	@pass   = split(/\//,$file);

	printf ("%s,num_congestion_detection,%d,whole lines,%d,ratio_detecttion_error,%.3f\n"
		,$pass[$#pass]
		,$amount
		,$num_of_line
		,$ratio_detecttion_error);
	
}

exit;
