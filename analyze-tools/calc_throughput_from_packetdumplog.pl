#!/usr/bin/perl

# packetdump.c のログを入力ファイルとする
# 1秒ごとの
# ・合計受信スループット
# ・各マシンからの受信スループット
#  を算出して表示　単位はkbit/sec

foreach $file (@ARGV) {
        open IN, $file or die "cannot open $file ($!)";

        chomp(@line = <IN>);
	$sequence_num = 0;
	$old_time     = 0;
        foreach (@line)
        {		
                s/[\r\n]//g;
                @field  = split(/ +/,$_);
		
                #field2はパケット受信時間に関わる項目
		#@field2 = split('[/:/]',$field[0]);
		
		if ($old_time==0) {#初期条件
		    #$old_time = sprintf $field2[2];
		    $old_time  = sprintf $field[0];
		}
		#$current_time = sprintf $field2[2];
		$current_time  = sprintf $field[0];
		#if($current_time < $old_time) {
		#    $current_time = $current_time + 60.0;
		#}
		#printf("%lf\n",$current_time);
		if (abs($current_time - $old_time) >= 1.0) { #1秒間の集計が終わり
		    #printf("sequence_num:%d,current:%lf,old:%lf,abs($current_time - $old_time):%lf\n",$sequence_num,$current_time,$old_time,abs($current_time - $old_time));
		    $old_time = $current_time;
		    $sequence_num++;
		}
		$list_count = 0;
		$list_max = @machine_list;
		$in_list = 0;
                #Add datasize to the each machine data.
		while ($list_count < $list_max) {
		    if ($field[1] eq $machine_list[$list_count]) {
			$in_list = 1;
			$throughput_each[$list_count][$sequence_num] 
			    += int($field[6])*8.0/1000;
			break;
		    }
		    $list_count++;
		}
		# If the machine is not in the machine list. it is new machine.  
		#if ($list_count >= $list_max) { 
		if ( $in_list == 0 ) {
		    $machine_list[$list_count] = $field[1];
		    $throughput_each[$list_count][$sequence_num] 
			+= int($field[7])*8.0/1000;
		}
	}

        close IN;
	
	printf ("####Throughput of each machine.################\n");

	$j = 0;
	while ($j < $list_max) {	
		$i = 0;
		printf("-----Machine %s-------\n", $machine_list[$j] );
		while ($i < $sequence_num) {
			printf ("sequence(%d),%.3f,kbps\n",$i+1,$throughput_each[$j][$i]);
			$i++;
		}
		$j++;
	}
	
	
}

exit;

