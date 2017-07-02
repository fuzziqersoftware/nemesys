CXX=g++
CXXLD=g++
OBJECTS=Main.o AMD64Assembler.o SourceFile.o PythonLexer.o PythonParser.o PythonASTNodes.o PythonASTVisitor.o Environment.o Analysis.o BuiltinFunctions.o BuiltinTypes.o AnnotationVisitor.o AnalysisVisitor.o CompilationVisitor.o
CXXFLAGS=-g -Wall -Werror -std=c++14 -I/opt/local/include
LDFLAGS=-L/opt/local/lib -lphosg
EXECUTABLE=nemesys

all: $(EXECUTABLE) AMD64AssemblerTest

$(EXECUTABLE): $(OBJECTS)
	$(CXXLD) $(LDFLAGS) -o $(EXECUTABLE) $^

AMD64AssemblerTest: AMD64Assembler.o AMD64AssemblerTest.o
	$(CXXLD) $(LDFLAGS) -o AMD64AssemblerTest $^

clean:
	rm -rf *.o $(EXECUTABLE) AMD64AssemblerTest

.PHONY: clean
