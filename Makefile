CC=g++
OBJECTS=main.o lex.o ast.o ast_visitor.o parse.o source_file.o env.o exec.o
CPPFLAGS=-g -Wall
LDFLAGS=
EXECUTABLE=nemesys

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	g++ $(LDFLAGS) -o $(EXECUTABLE) $^

clean:
	-rm *.o $(EXECUTABLE)

.PHONY: clean
