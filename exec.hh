#ifndef _EXEC_HH
#define _EXEC_HH

#include <map>
#include <string>

#include "ast.hh"
#include "env.hh"

void import_module(string module_name, CompoundStatement* module, GlobalEnvironment* global);
void exec_tree(CompoundStatement* suite, LocalEnvironment* local);

#endif // _EXEC_HH
