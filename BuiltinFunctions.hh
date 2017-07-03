#pragma once

#include <string>
#include <unordered_map>

#include "Environment.hh"
#include "Analysis.hh"

extern const std::unordered_map<std::string, Variable> builtin_names;
extern std::unordered_map<int64_t, FunctionContext> builtin_function_definitions;
