#!/usr/bin/perl

# clientd実行コマンド(nohupして)

#nohup.outを消す
$del_nohupout    = " rm nohup.out -rf";
system(${del_nohupout});
$command         = "nohup ./clientd &";
#実行
system(${command});
#VLCの実行状況(webカメラのデバイスネームが違うなどで起動しないことがある)
$confirm_status  = "grep \"fail\" nohup.out";
system(${confirm_status});
