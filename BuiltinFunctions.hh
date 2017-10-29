#pragma once

#include <string>
#include <unordered_map>

#include "Environment.hh"
#include "Analysis.hh"
#include "Types/Instance.hh"


#define void_fn_ptr(a) reinterpret_cast<const void*>(+a)


extern std::unordered_map<std::string, Variable> builtin_names;
extern std::unordered_map<int64_t, FunctionContext> builtin_function_definitions;
extern std::unordered_map<int64_t, ClassContext> builtin_class_definitions;

extern InstanceObject MemoryError_instance;
extern int64_t AssertionError_class_id;
extern int64_t IndexError_class_id;
extern int64_t KeyError_class_id;
extern int64_t OSError_class_id;
extern int64_t TypeError_class_id;
extern int64_t ValueError_class_id;

extern int64_t BytesObject_class_id;
extern int64_t UnicodeObject_class_id;
extern int64_t DictObject_class_id;
extern int64_t ListObject_class_id;
extern int64_t TupleObject_class_id;
extern int64_t SetObject_class_id;

// functions for creating new builtin functions and classes
int64_t create_builtin_function(BuiltinFunctionDefinition& def);
int64_t create_builtin_class(BuiltinClassDefinition& def);

void create_builtin_name(const char* name, const Variable& value);

void create_default_builtin_names();

// functions for retrieving builtin names
std::shared_ptr<ModuleAnalysis> get_builtin_module(const std::string& module_name);
