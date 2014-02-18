#!/usr/bin/perl

# nohup.logを確認するだけ

$confirm_com  = "grep \"received beacon\" nohup.out";
system(${confirm_com});
