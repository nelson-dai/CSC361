.phony all:
all: rdps rdpr

rdps: rdps.c
	gcc -o rdps rdps.c 
rdpr: rdpr.c
	gcc -o rdpr rdpr.c  
.PHONY clean:
clean:
	-rm -rf *.o *.exe
