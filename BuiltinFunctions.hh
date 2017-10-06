#pragma once

#include <string>
#include <unordered_map>

#include "Environment.hh"
#include "Analysis.hh"
#include "Types/Instance.hh"


#define void_fn_ptr(a) reinterpret_cast<const void*>(+a)


// indexes of built-in functions and some commonly-used ids and instances
extern std::unordered_map<std::string, Variable> builtin_names;
extern std::unordered_map<int64_t, FunctionContext> builtin_function_definitions;
extern std::unordered_map<int64_t, ClassContext> builtin_class_definitions;

extern InstanceObject MemoryError_instance;
extern int64_t AssertionError_class_id;
extern int64_t IndexError_class_id;
extern int64_t KeyError_class_id;
extern int64_t OSError_class_id;

// functions for creating new builtin functions and classes
int64_t create_builtin_function(const char* name,
    const std::vector<Variable>& arg_types, const Variable& return_type,
    const void* compiled, bool pass_exception_block, bool register_globally);
int64_t create_builtin_function(const char* name,
    const std::vector<FunctionContext::BuiltinFunctionFragmentDefinition>& fragments,
    bool pass_exception_block, bool register_globally);
int64_t create_builtin_class(const char* name,
    const std::map<std::string, Variable>& attributes,
    const std::vector<Variable>& init_arg_types, const void* init_compiled,
    const void* destructor, bool register_globally);

void create_builtin_name(const char* name, const Variable& value);

void create_default_builtin_names();

// functions for creating some common objects
void* create_IndexError(int64_t index);

// functions for retrieving builtin names
std::shared_ptr<ModuleAnalysis> get_builtin_module(const std::string& module_name);
