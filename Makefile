.PHONY: fscheck
fscheck: fscheck.c
	gcc -c -Wall fscheck.c fscheck.o 
	
.PHONY: clean
clean:
	rm -rf fscheck.o

