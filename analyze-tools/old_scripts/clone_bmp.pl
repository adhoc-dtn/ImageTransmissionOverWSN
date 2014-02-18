#!/usr/bin/perl

# 機能 ：bmpファイルをクローンする 
# 使い方: $ perl clone_picture (ファイル名.bmp) (クローン数)  (出力先ディレクトリ)
# 出力  : .bmp


if (scalar(@ARGV)<3 || scalar(@ARGV)>3) {
    die "<Usage>: perl clone_picture.pl (target file) (number of file for clone) (output_dir)\n";
}

my $target  = $ARGV[0];
my $num_clone = $ARGV[1];
my $output_dir = $ARGV[2];

$i = 0;
printf("$target x $num_clone -> output dir $output_dir\n");
while ($i < $num_clone) {
    my $filename = sprintf("%s/%06d.bmp",$output_dir,$i++);
    printf("$filename criate\n");

    system ("cp $target $filename;");
}

printf("clone is done.\n");

