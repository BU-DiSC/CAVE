CC = clang++

RM = rm
OBJS = Serializer.o Graph.o BlockCache.o SegmentTree.o CacheSimple.o
CFLAGS = -Wall -g -O3 -march=native -pthread -std=c++17

ifeq ($(OS),Windows_NT)
	LFLAGS = 
	EXE_SUFFIX = .exe
else
	LFLAGS = -laio
	EXE_SUFFIX = 
endif

.PHONY: clean

all: bin/parser$(EXE_SUFFIX) bin/wcc$(EXE_SUFFIX) bin/bfs$(EXE_SUFFIX) bin/dfs$(EXE_SUFFIX) bin/tc$(EXE_SUFFIX) bin/pagerank$(EXE_SUFFIX) bin/randomwalk$(EXE_SUFFIX)

bin/parser$(EXE_SUFFIX): parser.o $(OBJS)
	$(CC) $(CFLAGS) $(LFLAGS) $(OBJS) $< -o $@

bin/wcc$(EXE_SUFFIX): wcc.o $(OBJS)
	$(CC) $(CFLAGS) $(LFLAGS) $(OBJS) $< -o $@

bin/bfs$(EXE_SUFFIX): bfs.o $(OBJS)
	$(CC) $(CFLAGS) $(LFLAGS) $(OBJS) $< -o $@

bin/dfs$(EXE_SUFFIX): dfs.o $(OBJS)
	$(CC) $(CFLAGS) $(LFLAGS) $(OBJS) $< -o $@

bin/tc$(EXE_SUFFIX): tc.o $(OBJS)
	$(CC) $(CFLAGS) $(LFLAGS) $(OBJS) $< -o $@

bin/pagerank$(EXE_SUFFIX): pagerank.o $(OBJS)
	$(CC) $(CFLAGS) $(LFLAGS) $(OBJS) $< -o $@

bin/randomwalk$(EXE_SUFFIX): randomwalk.o $(OBJS)
	$(CC) $(CFLAGS) $(LFLAGS) $(OBJS) $< -o $@

wcc.o: algorithm/wcc.cpp
	$(CC) $(CFLAGS) -c $<

bfs.o: algorithm/bfs.cpp
	$(CC) $(CFLAGS) -c $<

dfs.o: algorithm/dfs.cpp
	$(CC) $(CFLAGS) -c $<

tc.o: algorithm/tc.cpp
	$(CC) $(CFLAGS) -c $<

pagerank.o: algorithm/pagerank.cpp
	$(CC) $(CFLAGS) -c $<

randomwalk.o: algorithm/randomwalk.cpp
	$(CC) $(CFLAGS) -c $<

parser.o: parser.cpp
	$(CC) $(CFLAGS) -c $<

main.o: main.cpp
	$(CC) $(CFLAGS) -c $<

Serializer.o: Serializer.cpp Serializer.hpp
	$(CC) $(CFLAGS) -c $<

BlockCache.o: BlockCache.cpp BlockCache.hpp
	$(CC) $(CFLAGS) -c $<

CacheSimple.o: CacheSimple.cpp CacheSimple.hpp
	$(CC) $(CFLAGS) -c $<

SegmentTree.o: SegmentTree.cpp SegmentTree.hpp
	$(CC) $(CFLAGS) -c $<

Graph.o: Graph.cpp Graph.hpp
	$(CC) $(CFLAGS) -c $<

clean:
	$(RM) *.o
