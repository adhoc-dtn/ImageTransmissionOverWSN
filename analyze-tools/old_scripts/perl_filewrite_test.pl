#!/usr/bin/perl

# use strict;
# use warnings;
use utf8;
use open ":utf8";
binmode STDIN, ':encoding(cp932)';
binmode STDOUT, ':encoding(cp932)';
binmode STDERR, ':encoding(cp932)';

open(DATAFILE, "> data5-1.txt") or die("Error:$!");

print DATAFILE "大阪\n";
sleep(20);
print DATAFILE "Osaka\n";
close(DATAFILE);
