#pragma once

#include <memory>

#include "../Analysis.hh"

extern std::shared_ptr<ModuleAnalysis> __nemesys___module;

void __nemesys___set_global(std::shared_ptr<GlobalAnalysis> new_global);
void __nemesys___initialize();
