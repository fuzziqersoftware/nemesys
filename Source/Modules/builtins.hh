#pragma once

#include <memory>

#include "../Compiler/Contexts.hh"

std::shared_ptr<ModuleContext> builtins_initialize(GlobalContext* global);
