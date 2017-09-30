all:
	gcc -g -Wall myhttpd.c -o myhttpd
clean:
	rm -rf myhttpd myhttpd.dSYM
