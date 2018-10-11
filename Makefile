P= smmfdp

CC= gcc
CFLAGS= -std=c99 -Wall
OBJECTS= bwt.o divsufsort.o mem_seed_prob.o random.o

all: CFLAGS += -DNDEBUG -O3
all: $(P)

debug: CFLAGS += -DDEBUG -g -O0
debug: $(P)

$(P): smmfdp.c bwt.h $(OBJECTS)
	$(CC) $(CFLAGS) smmfdp.c $(OBJECTS) -o $(P) -lm

clean:
	rm -f $(OBJECTS) $(P)
