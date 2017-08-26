#pragma once

#include <memory>
#include <vector>

#include "../Analysis.hh"

extern std::shared_ptr<ModuleAnalysis> posix_module;
void posix_initialize();
