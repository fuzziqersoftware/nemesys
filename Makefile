CC=g++
OBJECTS=main.o source_file.o
CFLAGS=-g -Wall
LDFLAGS=
EXECUTABLE=nemesys

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	g++ $(LDFLAGS) -o $(EXECUTABLE) $^

clean:
	-rm *.o $(EXECUTABLE)

.PHONY: clean
