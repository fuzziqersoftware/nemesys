#pragma once

#include <memory>
#include <vector>

#include "../Compiler/Contexts.hh"

extern std::shared_ptr<ModuleContext> posix_module;

void posix_initialize();
