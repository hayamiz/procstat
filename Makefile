

CFLAGS := -O0 -g -Wall


.PHONY: all clean

all: procstat

clean:
	rm *.o
	rm procstat

procstat: procstat.o
