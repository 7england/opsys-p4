CC = g++
CFLAGS = -Wall -g
TARGETS = oss worker
all: $(TARGETS)
oss: oss.o
    $(CC) $(CFLAGS) -o oss oss.o
worker: worker.o
    $(CC) $(CFLAGS) -o worker worker.o
clean:
    rm -f *.o $(TARGETS)
oss.o: oss.cpp
    $(CC) $(CFLAGS) -c oss.cpp
worker.o: worker.cpp
    $(CC) $(CFLAGS) -c worker.cpp
.PHONY: all clean