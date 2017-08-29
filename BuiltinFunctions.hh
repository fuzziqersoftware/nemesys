#pragma once

#include <string>
#include <unordered_map>

#include "Environment.hh"
#include "Analysis.hh"

extern std::unordered_map<std::string, Variable> builtin_names;
extern std::unordered_map<int64_t, FunctionContext> builtin_function_definitions;
extern std::unordered_map<int64_t, ClassContext> builtin_class_definitions;

int64_t create_builtin_function(const char* name,
    const std::vector<Variable>& arg_types, const Variable& return_type,
    const void* compiled, bool register_globally);
int64_t create_builtin_function(const char* name,
    const std::vector<FunctionContext::BuiltinFunctionFragmentDefinition>& fragments,
    bool register_globally);
int64_t create_builtin_class(const char* name,
    const std::map<std::string, Variable>& attributes,
    const std::vector<Variable>& init_arg_types, const void* init_compiled,
    const void* destructor, bool register_globally);

void create_builtin_name(const char* name, const Variable& value);

void create_default_builtin_names();

std::shared_ptr<ModuleAnalysis> get_builtin_module(const std::string& module_name);
