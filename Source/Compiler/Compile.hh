#pragma once

#include <memory>
#include <unordered_map>
#include <string>

#include "Contexts.hh"
#include "../Environment/Value.hh"

void advance_module_phase(GlobalContext* global, ModuleContext* module,
    ModuleContext::Phase phase);

FunctionContext::Fragment compile_scope(GlobalContext* global,
    ModuleContext* module, FunctionContext* fn = NULL,
    const std::unordered_map<std::string, Value>* local_overrides = NULL);
