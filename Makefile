all:
	gcc -g -Wall myhttpd.c -o myhttpd
	gcc -g -Wall webclient.c -o webclient
	gcc -g -Wall wget.c -o wget
	gcc -g -Wall -std=c99 hello.c -o hello
clean:
	rm -rf myhttpd myhttpd.dSYM webclient webclient.dSYM wget.dSYM wget hello hello.dSYM
