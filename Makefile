multiproxy: multiproxy.c
	gcc -Wall -D LOG multiproxy.c -o multiproxy.out

nolog: multiproxy.c
	gcc -Wall  multiproxy.c -o multiproxy.out
