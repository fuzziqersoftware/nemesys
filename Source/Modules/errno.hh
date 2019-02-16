#pragma once

#include <memory>

#include "../Compiler/Contexts.hh"

std::shared_ptr<ModuleContext> errno_initialize(GlobalContext* global);
