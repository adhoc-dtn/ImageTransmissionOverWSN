use Class::Struct;

# status of stream
# last_isnt_end 1: その行で最後 0: その行に0xffd9がない
struct statusStream => {
    eachStream    => '@',
    last_isnt_end => '$',
};
 
my $stat_stream = new statusStream();

$inputfile = "binary.jpg";
# splitter
$endOfJpeg = 0xffd9;

# open stream
open (IN, $inputfile) or die "$!";

$sequence_picture= 1;
#書き込み中
$middle_of_write = 0;
$max_line        = 0;


while (<IN>) {
    my $stat_stream = splitAtTheEnd($endOfJpeg, $_);
    my $binary = unpack("H*",$_);
    #printf("%s\n",$binary);
    $max_line = @{$stat_stream->eachStream};
#    printf("max line %d\n copy %s"
#	   ,$max_line
#	   ,unpack("H*", $stat_stream->eachStream(0)) );
    if (@{$stat_stream->eachStream}) {    # not empty
	if ($middle_of_write == 0) {      # 新規ファイル作成
	    $filenumber = sprintf("%05d", $sequence_picture++);
	    $jpegfile   = sprintf("output%s.jpg", $filenumber);
	    open (OUT, ">$jpegfile") or die "$!";
	}
	$number_of_lines  = 0;
	foreach my $eachStream (@{$stat_stream->eachStream}) {
#	    printf("\n[line\n %s\n",$eachStream);
		    
	    print OUT $eachStream;
	    if((++$number_of_lines) >= $max_line) {
		if($stat_stream->last_isnt_end == 0) {
		    close(OUT);
		    #printf("\n[end] %s\n", unpack ("H*", $eachStream));
		    #printf("\n[jpeg file is created %s]\n",$jpegfile);
		    $filenumber = sprintf("%05d", $sequence_picture++);
		    $jpegfile   = sprintf("output%s.jpg",$filenumber);
		    open (OUT, ">$jpegfile") or die "$!";
		    $middle_of_write = 0;
		} else {
		    $middle_of_write = 1;
		    #printf("[middle of write]\n");
		}
	    } else {
		printf("\n[jpeg file is created %s]\n",$jpegfile);
		close(OUT);
		#printf("\n[end] %s\n", unpack ("H*", $eachStream));

		$filenumber = sprintf("%05d", $sequence_picture++);
		$jpegfile   = sprintf("output%s.jpg",$filenumber);
		open (OUT, ">$jpegfile") or die "$!";
		$middle_of_write = 1;
	    }
	}
    }
} 

close(IN);

#--------------------------------------------------------------
# splitAtTheEnd (spliter, stream);
#   機能  : stream(ビット列)をspliter(16進数)がつく直前までの
#          各ストリームに分割
#   引数  : spliter(分割パターン), stream(ストリーム)
#   返り値: @output_lines(最後尾にspliterがつくストリームのリスト)
#--------------------------------------------------------------
sub splitAtTheEnd {
    (my $d_spliter, my $stream) = @_;
    
    my $statstream      = new statusStream();
    # ストリームを16進文字列に変換
    my $residual_stream = unpack("H*",$stream); #hだと4bit逆になる
    my $spliter         = sprintf("%x",$d_spliter);
    
    my $ln = 0;

    my $old_line = $residual_stream;
    while (1) {
	# ただのindex使うとまずい
	my $split_at = index_byte($residual_stream, $spliter);
	#printf("split at %d\n", $split_at);
	if ( $split_at<0 ) {
	    if ($residual_stream){
		# 分割したストリームをバイナリに戻す
		$statstream->eachStream($ln++, pack("H*", $residual_stream)); 
		$statstream->last_isnt_end(1);
		#printf("statstream last %d\n",$statstream->last_isnt_end);
	    } else {
		$statstream->last_isnt_end(0);
		#printf("old line %s\n",$old_line);
	    }
	    last;
	} else {
	    $split_at        += length $spliter;
	    
	    $eachline         = substr($residual_stream, 0, $split_at);
	    $old_line = $eachline;
#	    printf("eachline %s\n", $spliter, $eachline);
	    # 分割したストリームをバイナリに戻す
	    $statstream->eachStream($ln++, pack("H*", $eachline)); 
	    
	    $residual_stream  = substr($residual_stream, $split_at);
	} 
    }

    return $statstream;
}

#--------------------------------------------------------------
# index_byte(hexstream, pattern)
#     機能  : hexstream中でpatternをみつけると直前のindexを返す
#     引数  : hexstream  16進文字列(1文字4bitを表す)
#             pattern    パターン16進文字列
#     返り値: (マッチングあり) マッチした場所のインデックス
#                       なし  -1
#--------------------------------------------------------------
sub index_byte {
    (my $hexstream, my $pattern) = @_;

    my $residual_string = $hexstream;
    my $index = 0;
    while (length($residual_string) - length($pattern)>0)  {
	my $target       = substr($residual_string, 0, length($pattern));
	$residual_string = substr($residual_string, 2); #残りの部分は1byte=2文字分ずらす

	if ($target eq $pattern) {
	    printf("cut must be  %s\n"
		   ,substr($hexstream, 0, $index+length($pattern)));
	    return $index;
	}else { 
	    $index+=2; #1byte分=2文字分ずらす
	}
    }
    return -1;
}
