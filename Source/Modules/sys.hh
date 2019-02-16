#pragma once

#include <memory>
#include <vector>

#include "../Compiler/Contexts.hh"

void sys_set_executable(const char* realpath);
void sys_set_argv(const std::vector<const char*>& sys_argv);
std::shared_ptr<ModuleContext> sys_initialize(GlobalContext* global);
