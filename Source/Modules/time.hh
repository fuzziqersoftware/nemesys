#pragma once

#include <memory>

#include "../Compiler/Contexts.hh"

extern std::shared_ptr<ModuleContext> time_module;

void time_initialize();
