A tool to create a pair of virtual ttys


Compile:

	make -j4

Usage:

	[sudo] insmod vttys.ko



When loaded, create 8 ttys interconnected:

	/dev/tnt0 <=> /dev/tnt1

	/dev/tnt2 <=> /dev/tnt3

	/dev/tnt4 <=> /dev/tnt5

	/dev/tnt6 <=> /dev/tnt7
