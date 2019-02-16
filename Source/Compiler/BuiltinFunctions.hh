#pragma once

#include <string>
#include <unordered_map>

#include "Contexts.hh"
#include "../Types/Instance.hh"


extern InstanceObject MemoryError_instance;

#define void_fn_ptr(a) reinterpret_cast<const void*>(+a)

std::shared_ptr<ModuleContext> create_builtin_module(GlobalContext* global,
    const std::string& module_name);
