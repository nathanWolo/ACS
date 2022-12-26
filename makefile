.phony all:
all: ACS

ACS: ACS.c
	gcc -o ACS ACS.c -lpthread -g -Wall

.PHONY clean:
clean:
	-rm -rf *.o *.exe
