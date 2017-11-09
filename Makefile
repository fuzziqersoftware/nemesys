CXX=g++
CXXLD=g++
OBJECTS=Main.o Debug.o \
	Assembler/CodeBuffer.o Assembler/AMD64Assembler.o \
	Parser/SourceFile.o Parser/PythonLexer.o Parser/PythonParser.o Parser/PythonOperators.o Parser/PythonASTNodes.o Parser/PythonASTVisitor.o \
	Types/Reference.o Types/Strings.o Types/Format.o Types/Tuple.o Types/List.o Types/Dictionary.o Types/Instance.o \
	Modules/__nemesys__.o Modules/sys.o Modules/math.o Modules/posix.o Modules/errno.o Modules/time.o \
	Environment.o Analysis.o \
	BuiltinFunctions.o CommonObjects.o \
	Exception.o Exception-Assembly.o \
	AnnotationVisitor.o AnalysisVisitor.o CompilationVisitor.o
CXXFLAGS=-g -Wall -Werror -std=c++14 -I/opt/local/include
LDFLAGS=-L/opt/local/lib -lphosg

all: Assembler/amd64dasm test

test: nemesys Assembler/AMD64AssemblerTest Types/DictionaryTest
	./Assembler/AMD64AssemblerTest
	./Types/DictionaryTest
	(cd tests ; ./run_tests.sh)

nemesys: $(OBJECTS)
	$(CXXLD) $(LDFLAGS) -o nemesys $^

Assembler/AMD64AssemblerTest: Assembler/CodeBuffer.o Assembler/AMD64Assembler.o Assembler/AMD64AssemblerTest.o
	$(CXXLD) $(LDFLAGS) -o Assembler/AMD64AssemblerTest $^

Assembler/amd64dasm: Assembler/AMD64Assembler.o Assembler/Main.o
	$(CXXLD) $(LDFLAGS) -o Assembler/amd64dasm $^

Types/DictionaryTest: Types/DictionaryTest.o Debug.o Types/Dictionary.o Types/Strings.o Types/Reference.o Types/Instance.o Exception.o Exception-Assembly.o
	$(CXXLD) $(LDFLAGS) -o Types/DictionaryTest $^

clean:
	rm -rf *.o nemesys AMD64AssemblerTest nemesys.dSYM Assembler/*.o Assembler/*Test Parser/*.o Modules/*.o Types/*.o Types/*Test

.PHONY: all clean test
