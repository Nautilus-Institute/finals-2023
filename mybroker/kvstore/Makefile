CC = g++
CFLAGS = -std=c++14 -Wall -I/usr/local/include -O2 -DDEBUG -fno-stack-protector
LDFLAGS = -z execstack
TARGET = server
SOURCES = server.cpp \
    store.cpp
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
