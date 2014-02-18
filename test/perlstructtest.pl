use Class::Struct;
  
struct Person => {
    name   => '$',
    list   => '@'
};
 
my  $p = new Person();

$st   = "hogehogehogehoge";
$index = index($st,"hoge");
$index += length "hoge";
$fh    = substr($st,0,$index); 
$subs  = substr($st,$index); 

printf("%s\n",$fh);
printf("%s\n",$subs);
printf("len %d\n",length($fh));
#foreach my $line ( @{$p->list} ) {
#    printf("%s\n",$line);##
#}
