CC=gcc
CFLAGS=-I.
DEPS = proxy.h
OBJ = proxy.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS) -pthread

compile: publisher subscriber broker

subscriber: $(OBJ) subscriber.o 
	$(CC) -o subscriber $(OBJ) subscriber.o

publisher: $(OBJ) publisher.o 
	$(CC) -o publisher $(OBJ) publisher.o

broker: $(OBJ) broker.o 
	$(CC) -o broker $(OBJ) broker.o

clean:
	rm -f subscriber
	rm -f publisher
	rm -f broker