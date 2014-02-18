#!/usr/bin/perl

#nohup.outを消す
$del_nohupout    = " rm nohup.out -rf";
system(${del_nohupout});
$kill    = "killall vlc clientd serverd";
$del_com = "rm 1* logfile_* -rf";

system(${kill});
system(${del_com});
