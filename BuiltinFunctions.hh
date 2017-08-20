#pragma once

#include <string>
#include <unordered_map>

#include "Environment.hh"
#include "Analysis.hh"

extern const std::unordered_map<std::string, Variable> builtin_names;
extern std::unordered_map<int64_t, FunctionContext> builtin_function_definitions;
extern std::unordered_map<std::string, std::shared_ptr<ModuleAnalysis>> builtin_modules;

std::shared_ptr<ModuleAnalysis> get_builtin_module(const std::string& module_name);
