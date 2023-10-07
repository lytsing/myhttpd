all:
	gcc -g -Wall myhttpd.c -o myhttpd
	gcc -g -Wall webclient.c -o webclient
clean:
	rm -rf myhttpd myhttpd.dSYM webclient webclient.dSYM
