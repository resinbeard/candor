all:
	cp ficus/config.h .
	cp ficus/libficus.c .
	cp ficus/libficus.h .
	cp ficus/rtqueue.c .
	cp ficus/rtqueue.h .
	gcc -o candor main.c libficus.c rtqueue.c -llo -lsndfile -lasound -ljack -lpthread -lmonome
	rm libficus.c libficus.h rtqueue.c rtqueue.h config.h
install:
	cp candor /opt/bin/candor
uninstall:
	rm /opt/bin/candor
clean: 
	rm candor
