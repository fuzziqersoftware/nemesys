#pragma once

#include <string>

#include "ast.hh"
#include "env.hh"

using namespace std;

void import_module(string module_name, CompoundStatement* module, Environment* env);
void exec_tree(CompoundStatement* suite, Environment* env);
