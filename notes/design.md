# nemesys design

## Principles

Guiding ideas behind this project:
- This project is for my enjoyment and learning only. It's great if it also ends up providing something of value to the world, but that's not the primary objective.
- nemesys does not aim to support everything that CPython supports. Reasonable restrictions can be placed on the language that make it easier to statically analyze and compile; it is explicitly a non-goal of this project to support all existing Python code.
- With the above in mind, nemesys should operate on pure, unmodified Python code, without any auxiliary information (like Cython's .pxd files). All code that runs in nemesys should also run in CPython, unless it uses the `__nemesys__` built-in module.

## Type system

The basic types are:

    None                - NULL
    Bool                - true or false
    Int                 - signed 64-bit integer
    Float               - double-precision floating-point number
    Bytes               - arbitrary-length binary data
    Unicode             - arbitrary-length UTF-8 encoded data
    List[A]             - mutable collection of objects of type A
    Tuple[A, ...]       - immutable collection of objects of possibly-disparate types
    Set[K]              - mutable collection of objects of type K
    Dict[K, V]          - mutable mapping of keys of type K to values of type V
    Function[R, A, ...] - function returning type R with arguments of types A, ...
    Class[S, A, ...]    - definition of a user-defined type with superclass S and constructor arguments of types A, ...
    Instance[C]         - instantiated class object
    Module              - definition of an imported module

Future work: allow S to be a tuple (for multiple inheritance).

### Value representation

Nontrivial types are represented as pointers to allocated objects. All objects have the following header fields:

    uint64_t reference_count
    void (*destructor)()

All objects must be destructible (destructor must never be NULL).

Built-in types have structures defined in their respective header files (in the Source/Types directory). User-defined types follow the structure in Source/Types/Instance.hh, with attributes immediately following the class id. The memory order of attributes in an instance object follows the order in which the attributes are created in the source file; most often, this is the order they're set in the class' `__init__` function. However, the memory order of the attributes in a class instance object should be considered arbitrary and should not be relied upon (though it should be impossible to have such a dependency in Python code).

### Refcounting semantics

Unlike CPython, not everything is an object. Trivial types (None, booleans, integers, and floats) do not have reference counts because they're passed by value. Modules, functions, and classes also do not have reference counts because they are never deleted. Everything else has a reference count; this includes bytes objects, unicode objects, lists, tuples, sets, dicts, and class instance objects.

The reference count of an object includes all instances of pointers to that object, including instances in CPU registers. All functions that return references to objects return owned references. All functions compiled by nemesys that take objects as arguments accept only owned references, and will delete those references before returning (that is, the caller is responsible for adding references to arguments, but not deleting those references after the function returns). Some built-in functions take borrowed references as arguments; most notably, the built-in data structure functions take borrowed references to all of their arguments, and will not delete those references before returning.

## Conventions

### Calling convention

The nemesys calling convention is similar to the System V calling convention used by Linux and Mac OS, but is a bit more complex. nemesys' convention is mostly compatible with the System V convention, so nemesys functions can directly call C functions without unnecessary register shuffling. nemesys' register assignment is as follows:

    register = callee-save? = purpose
    rax      =      no      = int return value, temp values
    rcx      =      no      = 4th int arg, temp values
    rdx      =      no      = 3rd int arg, int return value (high), temp values
    rbx      =      yes     = collection pointer during iteration (for loops)
    rsp      =              = stack pointer
    rbp      =      yes     = frame pointer
    rsi      =      no      = 2nd int arg, temp values
    rdi      =      no      = 1st int arg, temp values
    r8       =      no      = 5th int arg, temp values
    r9       =      no      = 6th int arg, temp values
    r10      =      no      = temp values
    r11      =      no      = temp values
    r12      =      yes     = common object pointer
    r13      =      yes     = global space pointer
    r14      =      yes     = exception block pointer
    r15      =      yes     = active exception instance
    xmm0     =      no      = 1st float arg, float return value, temp values
    xmm1     =      no      = 2st float arg, float return value (high), temp values
    xmm2-7   =      no      = 3rd-8th float args (in register order), temp values
    xmm8-15  =      no      = temp values

Integer arguments beyond the 6th and floating-point arguments beyond the 8th are passed on the stack.

The special registers (r12-r15) are used as follows:
- Common objects are stored in a statically-allocated array pointed to by r12. this array contains handy stuff like pointers to malloc() and free(), the preallocated MemoryError singleton instance, etc. References to these are generated by the common_object_reference function when generating assembly code.
- Global variables are referenced by offsets from r13 (e.g. with `mov [r13 + X]` opcodes). Each module has a statically-allocated memory space for its globals. When calling a function in a different module, r13 is saved and updated by the caller to point to the new module's global space. The one instance where this does not apply is for user-defined object destructors - these are wrapped in lead-in and lead-out blocks that set up r13 correctly for the destructor call. (This is needed because destructors may be called from anywhere, and the caller does not know which module the destructor belongs to.)
- The active exception block pointer is stored in r14. Except at the very beginning and end of module root scope functions, r14 must never be zero when executing compiled Python code. The exception block defines where control should return to when an exception is raised. This includes all `except Something` blocks, but also all `finally` blocks and all function scopes (so destructors are properly called). See the definition of the ExceptionBlock structure in Exception.hh for more information.
- The active exception object is stored in r15. Most of the time this should be zero; it's only nonzero when a raise statement is being executed and is in the process of transferring control to an except block, or when a finally block or function destructor call block is running and there is an active exception. In the future, this special register can probably be removed.

### Function and class model

nemesys doesn't have multiple inheritance yet.

Every function and class has a unique ID that identifies it. These IDs share the same space. The `__init__` function for a class has a function ID equal to the class' ID, but otherwise, no function may have the same ID as a class.

Negative function and class IDs are assigned to built-in functions and classes. Functions and classes defined in Python source are assigned positive IDs dynamically. Zero is not a valid function or class ID, and is used to indicate the absence of a function or class; for example, free functions have a class id of zero.

A class does not have to define `__init__`. If a class doesn't define `__init__`, then it can't be instantiated by Python code, and attempting to do so will give a weird compiler error about missing function contexts. Please consider the environment and always define `__init__` for your classes. This will probably be fixed in the future.

If a built-in class doesn't define `__init__`, though, then it doesn't appear in the global namespace at all, so it can't be instantiated from Python code because Python code can't even refer to it. It can only be instantiated by C/C++ functions and returned to Python code.

A function may have multiple fragments. A fragment is a compiled implementation of a function with completely-defined argument types. This is how nemesys implements function polymorphism - a function's argument types aren't necessarily known at definition time, so the argument variables are left as indeterminate types during static analysis. Then at call compilation time, the types of all arguments are known, and this signature is used to refer to the correct fragment, which can then be compiled if necessary and called.

For example, the function `sum_all_arguments` in tests/functions.py ends up having 3 fragments when all the code in the root scope has been run - one that takes all ints, one that takes all unicode objects, and one that takes all ints except arguments 2, 3, and 4, which are floats. If another module later imports this module and calls the function with different combinations of argument types, more fragments will be generated and compiled for this function. Currently, the fragment search algorithm is linear-time, so compiling a call to a function with a lot of fragments can be slow. This will be fixed in the future.

Fragments may be incompletely compiled; that is, they may contain calls to the compiler and missing code that depends on the result of those calls. Currently this only happens when an uncompiled fragment is called. When such a callsite is executed, it instead calls into the compiler, which compiles both the called fragment and caller fragment. It then returns to the location in the (newly-recompiled) caller fragment immediately before the call to the (newly-compiled) callee fragment, and execution continues in the new version of the caller fragment. These locations in the fragments where control can jump from an old version of a fragment to a new version are called splits.

## Compilation procedure

nemesys compiles modules in multiple phases. Roughly described, the phases are as follows:
- Search (finding the source file on disk)
- Loading (loading the source file into memory)
- Parsing (converting the source file to an abstract syntax tree)
- Annotation (annotating the tree with function/class IDs and collecting variable names)
- Analysis (inferring variable types)
- Compilation (generating abstract assembly code)
- Assembly (converting abstract assembly into executable assembly)
- Execution (exactly what it sounds like)

The top-level implementation of all of these phases is in GlobalAnalysis::advance_module_phase in Analysis.cc. Some of these phases are very simple; the more complex phases are described in detail below.

### Parsing phase

We use a custom parser written specifically for this project. This is because I thought it was easier to do this than to re-learn flex and bison. Also I wrote this lexer and parser several years before I did any real compilation with this project. Many function; wow. Such code.

### Annotation phase

This is mostly implemented by AnnotationVisitor. This visitor walks the AST and does a few basic things:
- It assigns class IDs to all classes defined in the file.
- It assigns function IDs to all functions and lambdas defined in the file.
- It collects global names for the module and local names for all functions defined in the file (indexed by function ID).
- It collects attribute names for all class definitions.
- It collects all import statements so the relevant modules can be loaded and collected.

This visitor modifies the AST by adding annotations for function IDs. it does this only for definition nodes (ClassDefinition, FunctionDefinition, LambdaDefinition); it does not do this for FunctionCall nodes since they may refer to modules that are not yet imported.

When this phase is completed, the following statements are true:
- All functions, lambdas, and classes defined in the module have globally unique IDs.
- The sizes of the module's global space and all functions' local spaces are known.
- The set of attributes on all class definitions is finalized (no new attributes can be created later), and the sizes of instances of all defined class types are known.

### Analysis phase

This is implemented by AnalysisVisitor. This visitor walks the AST and attempts to infer the type and value of all variables. For some variables this won't be possible; it leaves those as Indeterminate or with an unknown value (but this will likely cause compilation errors in the next phase). If type annotations are given, it uses them to help infer other types.

The compilation phase uses this information to know which fragment to call for a FunctionCall node, to short-circuit `if` statements that are always true or false, and other useful things.

### Compilation phase

This is implemented by CompilationVisitor. This visitor walks the AST for a specific execution path - that is, it does not recur into definitions of any type. This is by far the most complex visitor; it maintains a lot of state as it follows the execution path. Multiple invocations of this visitor are often required to compile a single source file; one for the module root scope, one for each fragment called from the root scope (including fragments for functions defined in other modules).

Module root scopes compile into functions that take no arguments and return the active exception object if one was raised, or NULL if no exception was raised. Functions defined within modules expect r12-r14 to already be set up properly; see the calling convention description below for more information.

Space for all local variables is initialized at the beginning of the function's scope. Temporary variables may only live during a statement's execution; when a statement is completed, they are either copied to a local/global variable or destroyed. This means that no registers should be reserved across statement boundaries, and no references should be held in registers either.

This visitor enforces type annotations for function calls. If a caller attempts to pass an argument that doesn't match the target function's argument types, the caller's caller will get a NemesysCompilerError (since the error occurs during the caller's execution). If a function attempts to return a value that doesn't align with its type annotation, the caller will get a NemesysCompilerError as well.

### Assembly phase

This phase doesn't walk the AST, so it doesn't have a Visitor class. This phase is done by libamd64's AMD64Assembler, using the stream produced by CompilationVisitor. (CompilationVisitor actually generates the stream directly in the AMD64Assembler object as it works.)
