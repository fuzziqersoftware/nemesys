#pragma once

#include <memory>

#include "../Compiler/Contexts.hh"

extern std::shared_ptr<ModuleContext> math_module;

void math_initialize();
