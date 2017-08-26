CXX=g++
CXXLD=g++
OBJECTS=Main.o CodeBuffer.o AMD64Assembler.o SourceFile.o PythonLexer.o PythonParser.o PythonASTNodes.o PythonASTVisitor.o Environment.o Analysis.o BuiltinFunctions.o Types/Reference.o Types/Strings.o Types/List.o Types/Dictionary.o AnnotationVisitor.o AnalysisVisitor.o CompilationVisitor.o Modules/__nemesys__.o Modules/sys.o Modules/posix.o
CXXFLAGS=-g -Wall -Werror -std=c++14 -I/opt/local/include
LDFLAGS=-L/opt/local/lib -lphosg

all: test

test: nemesys AMD64AssemblerTest Types/DictionaryTest
	./AMD64AssemblerTest
	./Types/DictionaryTest
	./run_tests.sh

nemesys: $(OBJECTS)
	$(CXXLD) $(LDFLAGS) -o nemesys $^

AMD64AssemblerTest: CodeBuffer.o AMD64Assembler.o AMD64AssemblerTest.o
	$(CXXLD) $(LDFLAGS) -o AMD64AssemblerTest $^

Types/DictionaryTest: Types/DictionaryTest.o Types/Dictionary.o Types/Strings.o Types/Reference.o
	$(CXXLD) $(LDFLAGS) -o Types/DictionaryTest $^

clean:
	rm -rf *.o nemesys AMD64AssemblerTest nemesys.dSYM Modules/*.o Types/*.o Types/*Test

.PHONY: clean test
