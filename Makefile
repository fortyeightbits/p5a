.PHONY: fscheck
fscheck: fscheck.c fs.h types.h
	gcc fscheck.c -o fscheck
	
.PHONY: clean
clean:
	rm -rf fscheck

