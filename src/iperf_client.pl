#!/usr/bin/perl

if (scalar(@ARGV)<1 || scalar(@ARGV)>1) {
    die "<Usage>: perl iperf_client.pl (server ip address [ipv4]\n";
}
my $server = $ARGV[0];
my $command = "iperf -c ${server} -u -b 2M > q_link_server.log";
system (${command});

