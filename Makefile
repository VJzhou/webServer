
all : httpd

httpd : httpd.c
	gcc -g -W -Wall -o $@ $< -lpthread

run :
	./httpd

clean :
	rm httpd