CXX=g++
CXXLD=g++
OBJECTS=main.o lex.o ast.o ast_visitor.o parse.o source_file.o compile.o
CXXFLAGS=-g -Wall -Werror -std=c++11
LDFLAGS=
EXECUTABLE=nemesys

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CXXLD) $(LDFLAGS) -o $(EXECUTABLE) $^

clean:
	rm -rf *.o $(EXECUTABLE)

.PHONY: clean
