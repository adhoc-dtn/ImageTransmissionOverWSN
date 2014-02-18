#-----------------------------------------------------------------------
# gen_image_from_stream.pl (port) (output_directory)
#      機能: jpgのストリームを受信し、jpgファイルを作成する
#      引数: port : jpgストリーム受信用ポート
#-----------------------------------------------------------------------

# 送信側がフラグメントしないように送信するとファイルが作られないバグがある

#（懸案事項) アーキテクチャによってはpack("H*", -)をpack("h*", -)と
# しなければならないかも [packの引数を変更したらunpackも同様な変更が必要である]

use Class::Struct;  # struct をつかう
use Socket;         # Socket モジュールを使う
use threads; 
use threads::shared; #グローバル変数を使う

#受信データキュー
our @BufferQueue  :shared;


# status of stream
# last_isnt_end 1: その行で終端記号0xffd9がある, 0: その行に0xffd9がない
struct statusStream => {
    eachStream    => '@',
    last_isnt_end => '$',
};

if (scalar(@ARGV)<1 || scalar(@ARGV)>1) {
    die "<Usage>: perl gen_image_from_stream.pl (portnumber) (outputdir).\nEXIT... \n";
}

foreach $argments (@inbound_param) {
    printf("%s\n",$argments);
}
my $receiver_port = $ARGV[0];
my $output_dir    = $ARGV[1];


$recv_stream_thread1 = threads->new(\&recv_stream, $receiver_port);
create_jpg_images($receiver_port);

#---------------------------------------------------------------
# recv_stream (receiver_port);
#   機能  : receiver_portでUDPサーバをオープンし,受信したデータをキューにプッシュする
#   引数  : receiver_port 受信ポート番号
#   返り値: なし(ソケットが作成できない場合プログラムは終了する)
#---------------------------------------------------------------
sub recv_stream  {
    my @inbound_param = @_;
    my $receiver_port = $inbound_param[0];
    my $sock;
    socket($sock, PF_INET, SOCK_DGRAM, 0) or die "Cannot create Socket.\n";
    bind($sock, pack_sockaddr_in($receiver_port, INADDR_ANY));
    my $buffer_size    = 100000;
    my $buffer;
    printf("port %s\n",$receiver_port);
    while(1) {
	recv ($sock, $buffer, $buffer_size, 0);
	push @BufferQueue, $buffer;
    }
    
}

#--------------------------------------------------------------
# recv_jpg_images (port_number);
#   機能  : キュー(グローバル引数指定)中のjpgストリームからjpgファイルを
#           (output$port_number]ディレクトリに生成する
#   引数  : port_number : 受信ポート番号, 文字列で渡す       
#   返り値: 通信エラーが生じた場合,ディレクトリ作成できなかった場合 -1
#           
#--------------------------------------------------------------
sub create_jpg_images {
    my @inbound_param = @_;
    my $port_number   = $inbound_param[0];
   
    my $stat_stream = new statusStream();
    
# splitter
    my $endOfJpeg = 0xffd9;
    
    my $sequence_picture= 1;
#書き込み中
    my $middle_of_write = 0;
    my $max_line        = 0;
#mkdir
    my $dir_name = sprintf("output%s",$port_number);
    printf("dir_name : %s\n",$dir_name);

    mkdir "$dir_name";
    
    while (1) {
	
	while (scalar(@BufferQueue) == 0){} #wait for enqueue.
	$buffer_dequeue = shift @BufferQueue;

	printf("buffer(dequeue) length %d\n", length($buffer_dequeue) );
	my $stat_stream = splitAtTheEnd($endOfJpeg, $buffer_dequeue);

	if (@{$stat_stream->eachStream}) {    # not empty
	    if ($middle_of_write == 0) {      # 新規ファイル作成
		$jpegfile   = sprintf("%s/output%05d.jpg"
				      ,$dir_name, $sequence_picture++);
		open (OUT, ">$jpegfile") or die "$!";
	    }
	    $number_of_lines  = 0;
	    foreach my $eachStream (@{$stat_stream->eachStream}) {
	    #printf("\n[line\n %s\n last isnt end? %d\n",unpack ("H*", $eachStream), $stat_stream->last_isnt_end);
		
		print OUT $eachStream;
		if((++$number_of_lines) >= $max_line) {
		    if($stat_stream->last_isnt_end == 0) { #last is 0xffd9
			close(OUT);
			printf("\n[jpeg file is created %s](this is last line.)\n"
			       ,$jpegfile);
			$middle_of_write = 0;
		    } else {
			$middle_of_write = 1;
		    }
		} else {
		    #printf("\n[end] %s\n", unpack ("H*", $eachStream));
		    printf("\n[jpeg file is created %s]\n",$jpegfile);
		    close(OUT);
		    $jpegfile   = sprintf("%s/output%05d.jpg"
					  ,$dir_name, $sequence_picture++);
		    open (OUT, ">$jpegfile") or die "$!";
		    $middle_of_write = 1;
		}
	    }
	}
    } 

#    close(IN);
}

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
	#printf("split_at %d\n",$split_at);
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
	    #printf("residual %s\n",$residual_stream);
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
    while (length($residual_string)>0)  {
	my $target       = substr($residual_string, 0, length($pattern));
	$residual_string = substr($residual_string, 2); #残りの部分は1byte=2文字分ずらす

	if ($target eq $pattern) {
	    #printf("cut must be  %s\n"
	#	   ,substr($hexstream, 0, $index+length($pattern)));
	    return $index;
	}else { 
	    $index+=2; #1byte分=2文字分ずらす
	}
    }
    return -1;
}
