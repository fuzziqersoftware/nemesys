#pragma once

#include <memory>
#include <vector>

#include "../Analysis.hh"

extern std::shared_ptr<ModuleAnalysis> sys_module;
void sys_set_executable(const char* realpath);
void sys_set_argv(const std::vector<const char*>& sys_argv);
void sys_initialize();
