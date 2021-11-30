all:main.o
	gcc main.o -pthread -o main
	
main.o:main.c
	gcc -c main.c -pthread
