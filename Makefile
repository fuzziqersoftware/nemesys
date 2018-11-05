CXX=g++
CXXLD=g++
OBJECTS=Main.o Debug.o \
	Assembler/CodeBuffer.o Assembler/AMD64Assembler.o \
	AST/Environment.o AST/SourceFile.o AST/PythonLexer.o AST/PythonParser.o AST/PythonOperators.o AST/PythonASTNodes.o AST/PythonASTVisitor.o \
	Types/Reference.o Types/Strings.o Types/Format.o Types/Tuple.o Types/List.o Types/Dictionary.o Types/Instance.o \
	Modules/__nemesys__.o Modules/sys.o Modules/math.o Modules/posix.o Modules/errno.o Modules/time.o \
	Analysis.o \
	BuiltinFunctions.o CommonObjects.o \
	Exception.o Exception-Assembly.o \
	AnnotationVisitor.o AnalysisVisitor.o CompilationVisitor.o
CXXFLAGS=-g -Wall -Werror -std=c++14 -I/opt/local/include
LDFLAGS=-L/opt/local/lib
LIBS=-lphosg -lpthread

ifeq ($(shell uname -s),Darwin)
	CXXFLAGS += -DMACOSX
else
	CXXFLAGS += -DLINUX
endif

all: Assembler/amd64dasm Assembler/amd64asm test

test: nemesys Assembler/AMD64AssemblerTest Types/DictionaryTest
	./Assembler/AMD64AssemblerTest
	./Types/DictionaryTest
	(cd tests ; ./run_tests.sh)

nemesys: $(OBJECTS)
	$(CXXLD) $(LDFLAGS) -o nemesys $^ $(LIBS)

Assembler/AMD64AssemblerTest: Assembler/CodeBuffer.o Assembler/AMD64Assembler.o Assembler/AMD64AssemblerTest.o
	$(CXXLD) $(LDFLAGS) -o Assembler/AMD64AssemblerTest $^ $(LIBS)

Assembler/amd64dasm: Assembler/AMD64Assembler.o Assembler/amd64dasm.o
	$(CXXLD) $(LDFLAGS) -o Assembler/amd64dasm $^ $(LIBS)

Assembler/amd64asm: Assembler/AMD64Assembler.o Assembler/FileAssembler.o Assembler/amd64asm.o
	$(CXXLD) $(LDFLAGS) -o Assembler/amd64asm $^ $(LIBS)

Types/DictionaryTest: Types/DictionaryTest.o Debug.o Types/Dictionary.o Types/Strings.o Types/Reference.o Types/Instance.o Exception.o Exception-Assembly.o
	$(CXXLD) $(LDFLAGS) -o Types/DictionaryTest $^ $(LIBS)

clean:
	rm -rf *.o nemesys nemesys.dSYM Assembler/*.o Assembler/*Test Assembler/amd64dasm AST/*.o Modules/*.o Types/*.o Types/*Test

.PHONY: all clean test
