CC = gcc
CFLAGS = -Wall -I/usr/local/include -O2 -fno-stack-protector -z execstack -fno-pic -D_FORTIFY_SOURCE=0
LDFLAGS = -no-pie -z norelro -z execstack
TARGET = server
SOURCES = server.c
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	strip --strip-all $(TARGET)

$(OBJECTS): $(SOURCES)
	$(CC) $(CFLAGS) -c $(SOURCES)

clean:
	rm -f $(TARGET) $(OBJECTS)

.PHONY: all clean
