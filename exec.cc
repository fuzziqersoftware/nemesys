#include <tr1/memory>
#include <stdio.h>

#include <string>
#include <vector>
#include <set>

using namespace std;
using namespace std::tr1;

#include "ast.hh"
#include "ast_visitor.hh"
#include "env.hh"
#include "exec.hh"



struct LValueCollectionVisitor : ASTVisitor {

  bool in_lvalue;
  LocalEnvironment* env;
  set<string> explicit_globals;
  LValueCollectionVisitor(LocalEnvironment* _env) : in_lvalue(false), env(_env) { }

  void add_name(const string& name) {
    if (!explicit_globals.count(name))
      env->locals[name] = PyValue();
  }

  void visit(GlobalStatement* a) {
    for (int x = 0; x < a->names.size(); x++) {
      explicit_globals.insert(a->names[x]);
      if (env->locals.count(a->names[x]))
        env->locals.erase(a->names[x]);
    }
  }
  void visit(VariableLookup* a) {
    if (in_lvalue)
      add_name(a->name);
  }
  void visit(AssignmentStatement* a) {
    bool prev_in_lvalue = in_lvalue;
    in_lvalue = true;
    visit_list(a->left);
    in_lvalue = prev_in_lvalue;
    visit_list(a->right);
  }
  void visit(UnpackingVariable* a) {
    if (in_lvalue)
      add_name(a->name);
  }
  void visit(ForStatement* a) {
    bool prev_in_lvalue = in_lvalue;
    in_lvalue = true;
    a->variables->accept(this);
    in_lvalue = prev_in_lvalue;

    visit_list(a->in_exprs);
    visit_list(a->suite);
    if (a->else_suite)
      a->else_suite->accept(this);
  }
  void visit(ExceptStatement* a) {
    if (a->name.size())
      add_name(a->name);
  }

  // don't walk the children of these since they create internal scope
  void visit(FunctionDefinition* a) {
    add_name(a->name);
  }
  void visit(ClassDefinition* a) {
    add_name(a->name);
  }
  void visit(LambdaDefinition* a) { }
};

static void collect_variables(CompoundStatement* suite, LocalEnvironment* env) {
  LValueCollectionVisitor v(env);
  suite->accept(&v);
  for (map<string, PyValue>::iterator it = env->locals.begin(); it != env->locals.end(); it++)
    printf("LOCAL: %s = %s\n", it->first.c_str(), it->second.str().c_str());
}

void import_module(string module_name, CompoundStatement* module, GlobalEnvironment* global) {
  collect_variables(module, &global->modules[module_name]);
}

void exec_tree(CompoundStatement* suite, LocalEnvironment* local) {
  // TODO
}
