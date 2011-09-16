all:
	gcc -g -Wall myhttpd.c -o myhttpd
clean:
	rm -f myhttpd
