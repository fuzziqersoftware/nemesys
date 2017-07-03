CXX=g++
CXXLD=g++
OBJECTS=Main.o CodeBuffer.o AMD64Assembler.o SourceFile.o PythonLexer.o PythonParser.o PythonASTNodes.o PythonASTVisitor.o Environment.o Analysis.o BuiltinFunctions.o BuiltinTypes.o AnnotationVisitor.o AnalysisVisitor.o CompilationVisitor.o
CXXFLAGS=-g -Wall -Werror -std=c++14 -I/opt/local/include
LDFLAGS=-L/opt/local/lib -lphosg

all: test

test: nemesys AMD64AssemblerTest
	./AMD64AssemblerTest
	./nemesys_test.sh

nemesys: $(OBJECTS)
	$(CXXLD) $(LDFLAGS) -o nemesys $^

AMD64AssemblerTest: CodeBuffer.o AMD64Assembler.o AMD64AssemblerTest.o
	$(CXXLD) $(LDFLAGS) -o AMD64AssemblerTest $^

clean:
	rm -rf *.o nemesys AMD64AssemblerTest

.PHONY: clean test
