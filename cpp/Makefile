CC = clang++

RM = rm
OBJS = Serializer.o Graph.o GraphAlgorithm.o BlockCache.o SegmentTree.o
CFLAGS = -Wall -O3 -march=native -pthread -std=c++17


ifeq ($(OS),Windows_NT)
	LFLAGS = 
	EXE_SUFFIX = .exe
else
	LFLAGS = -laio
	EXE_SUFFIX = 
endif

.PHONY: clean

all: main$(EXE_SUFFIX) Parser$(EXE_SUFFIX)

main$(EXE_SUFFIX): $(OBJS) main.o
	$(CC) $(CFLAGS) $(LFLAGS) $(OBJS) main.o -o main$(EXE_SUFFIX)

Parser$(EXE_SUFFIX): $(OBJS) Parser.o
	$(CC) $(CFLAGS) $(LFLAGS) $(OBJS) Parser.o -o Parser$(EXE_SUFFIX)

Parser.o: Parser.cpp
	$(CC) $(CFLAGS) -c Parser.cpp

main.o: main.cpp
	$(CC) $(CFLAGS) -c main.cpp

Serializer.o: Serializer.cpp Serializer.hpp
	$(CC) $(CFLAGS) -c Serializer.cpp

BlockCache.o: BlockCache.cpp BlockCache.hpp
	$(CC) $(CFLAGS) -c BlockCache.cpp

SegmentTree.o: SegmentTree.cpp SegmentTree.hpp
	$(CC) $(CFLAGS) -c SegmentTree.cpp

Graph.o: Graph.cpp Graph.hpp
	$(CC) $(CFLAGS) -c Graph.cpp

GraphAlgorithm.o: GraphAlgorithm.cpp GraphAlgorithm.hpp
	$(CC) $(CFLAGS) -c GraphAlgorithm.cpp

clean:
	$(RM) *.o
