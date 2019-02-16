CXX=g++
CXXLD=g++
# TODO: this is bad. make real Makefiles in the subdirectories, you lazy bum
OBJECTS=Source/Debug.o \
	Source/Assembler/CodeBuffer.o Source/Assembler/AMD64Assembler.o \
	Source/AST/SourceFile.o Source/AST/PythonLexer.o Source/AST/PythonParser.o Source/AST/PythonASTNodes.o Source/AST/PythonASTVisitor.o \
	Source/Types/Reference.o Source/Types/Strings.o Source/Types/Format.o Source/Types/Tuple.o Source/Types/List.o Source/Types/Dictionary.o Source/Types/Instance.o \
	Source/Modules/builtins.o Source/Modules/__nemesys__.o Source/Modules/sys.o Source/Modules/math.o Source/Modules/posix.o Source/Modules/errno.o Source/Modules/time.o \
	Source/Environment/Operators.o Source/Environment/Value.o \
	Source/Compiler/Compile.o Source/Compiler/Compile-Assembly.o Source/Compiler/Contexts.o Source/Compiler/BuiltinFunctions.o Source/Compiler/CommonObjects.o Source/Compiler/Exception.o Source/Compiler/Exception-Assembly.o Source/Compiler/AnnotationVisitor.o Source/Compiler/AnalysisVisitor.o Source/Compiler/CompilationVisitor.o
CXXFLAGS=-g -Wall -Werror -std=c++14 -I/opt/local/include
LDFLAGS=-L/opt/local/lib
LIBS=-lphosg -lpthread

ifeq ($(shell uname -s),Darwin)
	CXXFLAGS += -DMACOSX
else
	CXXFLAGS += -DLINUX
endif

all: amd64dasm amd64asm nemesys test

test: nemesys Source/Assembler/AMD64AssemblerTest Source/Types/DictionaryTest
	./Source/Assembler/AMD64AssemblerTest
	./Source/Types/DictionaryTest
	(cd tests ; ./run_tests.sh)
	(cd tests_independent ; ./run_tests.sh)

nemesys: $(OBJECTS) Source/Main.o
	$(CXXLD) $(LDFLAGS) -o nemesys $^ $(LIBS)

Source/Assembler/AMD64AssemblerTest: Source/Assembler/CodeBuffer.o Source/Assembler/AMD64Assembler.o Source/Assembler/AMD64AssemblerTest.o
	$(CXXLD) $(LDFLAGS) -o Source/Assembler/AMD64AssemblerTest $^ $(LIBS)

amd64dasm: Source/Assembler/AMD64Assembler.o Source/Assembler/amd64dasm.o
	$(CXXLD) $(LDFLAGS) -o amd64dasm $^ $(LIBS)

amd64asm: Source/Assembler/AMD64Assembler.o Source/Assembler/FileAssembler.o Source/Assembler/amd64asm.o
	$(CXXLD) $(LDFLAGS) -o amd64asm $^ $(LIBS)

Source/Types/DictionaryTest: $(OBJECTS) Source/Types/DictionaryTest.o
	$(CXXLD) $(LDFLAGS) -o Source/Types/DictionaryTest $^ $(LIBS)

clean:
	rm -rf *.o nemesys amd64dasm amd64asm *.dSYM Source/Assembler/*.o Source/Assembler/*Test Source/*.o Source/AST/*.o Source/Environment/*.o Source/Compiler/*.o Source/Modules/*.o Source/Types/*.o Source/Types/*Test

.PHONY: all clean test
