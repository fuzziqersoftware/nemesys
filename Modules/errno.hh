#pragma once

#include <memory>

#include "../Analysis.hh"

extern std::shared_ptr<ModuleAnalysis> errno_module;
void errno_initialize();
