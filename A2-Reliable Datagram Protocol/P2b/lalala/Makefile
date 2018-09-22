.phony all:
all: rdps rdpr

rdps: rdps.c
	gcc -pthread -o rdps rdps.c 
rdpr: rdpr.c
	gcc -pthread -o rdpr rdpr.c  
.PHONY clean:
clean:
	-rm -rf *.o *.exe
