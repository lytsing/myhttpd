# myhttpd

A simple http web service for socket programming demo, write with C language.

Implement the excise of books 《Linux C编程一站式学习》： [5. 练习：实现简单的Web服务器](https://akaedu.github.io/book/ch37s05.html)


## Compile

```
make
```

## Run and Test

Start server:

```
./myhttpd
```

Test the URL request:

```
curl http://localhost:8000/index.html
```

or use browser to open it.

## Others

* `webclient.c` a simple web client connect to some host.
* `wget.c` A implify wget implement.

