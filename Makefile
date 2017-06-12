CXX=g++
CXXLD=g++
OBJECTS=Main.o SourceFile.o PythonLexer.o PythonParser.o PythonASTNodes.o PythonASTVisitor.o Environment.o CompilationVisitors.o
CXXFLAGS=-g -Wall -Werror -std=c++14 -I/opt/local/include
LDFLAGS=-L/opt/local/lib -lphosg
EXECUTABLE=nemesys

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CXXLD) $(LDFLAGS) -o $(EXECUTABLE) $^

clean:
	rm -rf *.o $(EXECUTABLE)

.PHONY: clean
