CC = g++
CFLAGS = -std=c++14 -Wall -I/usr/local/include -I../dependencies/sqlite_modern_cpp/hdr/ -Os -fno-pic -z execstack -fno-stack-protector
LDFLAGS = -lsqlite3 -lboost_system -lpthread -no-pie -z execstack
TARGET = inventory
SOURCES = inventory.cpp
OBJECTS = $(SOURCES:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	strip --strip-all $(TARGET)

$(OBJECTS): $(SOURCES)
	$(CC) $(CFLAGS) -c $(SOURCES)

clean:
	rm -f $(TARGET) $(OBJECTS)

.PHONY: all clean
