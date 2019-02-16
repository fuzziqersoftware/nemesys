#pragma once

#include <memory>
#include <vector>

#include "../Compiler/Contexts.hh"

std::shared_ptr<ModuleContext> posix_initialize(GlobalContext* global);
