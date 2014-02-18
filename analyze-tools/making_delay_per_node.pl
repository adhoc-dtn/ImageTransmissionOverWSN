#!/usr/bin/perl

my $number_of_files = 15;

my $command = "perl stat_calc_delay_time_correction.pl static_recv_data.log";

my $start;
my $node = 217;

for ($start = 0; $start < $number_of_files; $start++,$node++) {
       printf("Processing node number 192.168.30.$node\n");
    system "$command | grep 192.168.30.$node > ~/Dropbox/delay$node.csv" ;

}
