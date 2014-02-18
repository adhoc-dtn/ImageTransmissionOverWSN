$ip1 = 192.168.50.1;
$ip2 = 192.168.50.2;
$ip3 = 192.168.50.3;

$obj = 192.168.50.2;


while (scalar(%person) > 0 and ($key, $ref) = each(%person)) {
    print "$key / $ref->{'delay'} / $ref->{'start'}\n";
    if ($key eq $obj) {
	printf("$obj exists\n");
	last;
    } else {
	#printf("$obj is not exists\n");
    }
} 


$person{$ip1}{'delay'}  = 100;
$person{$ip1}{'start'}  =  0;
$person{$ip2}{'delay'}  = 200;
$person{$ip2}{'start'} =  10;
$person{$ip3}{'delay'}  = 20;
$person{$ip3}{'start'} =  5;


while (scalar(%person) > 0 and ($key, $ref) = each(%person)) {
    print "$key / $ref->{'delay'} / $ref->{'start'}\n";
    if ($key eq $obj) {
	printf("$obj exists\n");
	last;
    } else {
	#printf("$obj is not exists\n");
    }
}
