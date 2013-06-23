CC=g++
OBJECTS=main.o lex.o ast.o parse.o source_file.o
CPPFLAGS=-g -Wall
LDFLAGS=
EXECUTABLE=nemesys

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	g++ $(LDFLAGS) -o $(EXECUTABLE) $^

clean:
	-rm *.o $(EXECUTABLE)

.PHONY: clean
