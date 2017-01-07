#ifndef _EXEC_HH
#define _EXEC_HH

#include <string>

#include "ast.hh"
#include "env.hh"

using namespace std;

void import_module(string module_name, CompoundStatement* module, Environment* env);
void exec_tree(CompoundStatement* suite, Environment* env);

#endif // _EXEC_HH
