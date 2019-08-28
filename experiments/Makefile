CC = gcc
CFLAGS = -Wall -g
LIBS = -ljson
SRCS = test.c
EXE = test

all:
	$(CC) $(CFLAGS) -o $(EXE) $(SRCS) $(LIBS)

run: $(EXE)
	./$(EXE)
