.PHONY: fscheck
fscheck: fscheck.c
	gcc -c -Wall fscheck.c 
	
.PHONY: clean
clean:
	rm -rf fscheck.o

