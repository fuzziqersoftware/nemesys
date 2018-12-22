#pragma once

#include <memory>

#include "../Compiler/Contexts.hh"

extern std::shared_ptr<ModuleContext> errno_module;

void errno_initialize();
