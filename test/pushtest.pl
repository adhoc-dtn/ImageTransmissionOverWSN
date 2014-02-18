@foo = ("value 1", "value 2", "value 3");
@bar = ("value 4", "value %", "value 6");

push @foo, @bar;

printf("length %d\n", scalar(@foo));

while (scalar(@foo ) > 0) {
    printf("value : %s\n",shift(@foo));
}
