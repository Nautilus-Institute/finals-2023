.PHONY: all docker objects
all: bin/ulu objects

bin/ulu: src/*.c src/*.h lua-5.3.6/src/liblua.a bin/build Makefile
	mkdir -p bin
	clang -o bin/ulu.o -c src/run.c -I lua-5.3.6/src/ -std=c2x -Wno-format-security -fno-stack-protector
	clang -o bin/ulu bin/ulu.o lua-5.3.6/src/liblua.a -fno-stack-protector -lm -lcurses 
	strip -s bin/ulu

bin/build: src/build.c src/*.h lua-5.3.6/src/liblua.a
	mkdir -p bin
	clang -o bin/build src/build.c -I lua-5.3.6/src/ lua-5.3.6/src/liblua.a -lm -lcurses -std=c2x -g -Wno-format-security

lua-5.3.6:
	wget https://www.lua.org/ftp/lua-5.3.6.tar.gz
	tar -xzf lua-5.3.6.tar.gz

lua-5.3.6/src/liblua.a: lua-5.3.6
	cd lua-5.3.6 && make linux

objects: src/objects/* bin/build
	./bin/build

