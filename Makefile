CXX=g++
CXXLD=g++
OBJECTS=Main.o Debug.o \
	CodeBuffer.o AMD64Assembler.o \
	SourceFile.o PythonLexer.o PythonParser.o PythonOperators.o PythonASTNodes.o PythonASTVisitor.o \
	Environment.o Analysis.o \
	BuiltinFunctions.o CommonObjects.o \
	Exception.o Exception-Assembly.o \
	Types/Reference.o Types/Strings.o Types/Format.o Types/Tuple.o Types/List.o Types/Dictionary.o Types/Instance.o \
	AnnotationVisitor.o AnalysisVisitor.o CompilationVisitor.o \
	Modules/__nemesys__.o Modules/sys.o Modules/math.o Modules/posix.o Modules/errno.o Modules/time.o
CXXFLAGS=-g -Wall -Werror -std=c++14 -I/opt/local/include
LDFLAGS=-L/opt/local/lib -lphosg

all: test

test: nemesys AMD64AssemblerTest Types/DictionaryTest
	./AMD64AssemblerTest
	./Types/DictionaryTest
	(cd tests ; ./run_tests.sh)

nemesys: $(OBJECTS)
	$(CXXLD) $(LDFLAGS) -o nemesys $^

AMD64AssemblerTest: CodeBuffer.o AMD64Assembler.o AMD64AssemblerTest.o
	$(CXXLD) $(LDFLAGS) -o AMD64AssemblerTest $^

Types/DictionaryTest: Types/DictionaryTest.o Debug.o Types/Dictionary.o Types/Strings.o Types/Reference.o Types/Instance.o Exception.o Exception-Assembly.o
	$(CXXLD) $(LDFLAGS) -o Types/DictionaryTest $^

clean:
	rm -rf *.o nemesys AMD64AssemblerTest nemesys.dSYM Modules/*.o Types/*.o Types/*Test

.PHONY: clean test
