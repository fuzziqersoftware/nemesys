#include <memory>
#include <stdio.h>

#include <string>
#include <vector>
#include <unordered_set>

#include "ast.hh"
#include "ast_visitor.hh"
#include "env.hh"
#include "exec.hh"

using namespace std;



struct LValueCollectionVisitor : ASTVisitor {

  bool in_lvalue;
  LocalEnvironment* env;
  unordered_set<string> explicit_globals;
  LValueCollectionVisitor(LocalEnvironment* _env) : in_lvalue(false), env(_env) { }

  void add_name(const string& name) {
    if (!explicit_globals.count(name))
      env->names[name] = PyValue();
  }

  void visit(GlobalStatement* a) {
    for (int x = 0; x < a->names.size(); x++) {
      explicit_globals.insert(a->names[x]);
      if (env->names.count(a->names[x]))
        env->names.erase(a->names[x]);
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



struct ExecutionVisitor : ASTVisitor {
  Environment* env;
  Environment* module_env;

  PyValue* unpacking_stack = NULL;
  bool break_flag = false;
  bool continue_flag = false;
  PyValue* return_value = NULL;
  PyValue* exception = NULL;

  ExecutionVisitor(Environment* _env, Environment* _module_env) : env(_env), module_env(_module_env) { }

  void visit(UnpackingTuple* a) {
    // expect a tuple or list with the right number of dudes in it
    // unpack that dude and recur
  }
  void visit(UnpackingVariable* a) {
    // find the relevant variable and attach the value to it
    // do not create the variable - if it's not in the local env, then it must be global'd and we should look in parent envs
  }
  void visit(ArgumentDefinition* a) {}
  void visit(UnaryOperation* a) {
    // recur on the expression, then modify it
  }
  void visit(BinaryOperation* a) {
    // recur on the expressions, then combine them
  }
  void visit(TernaryOperation* a) {
    // recur on the center expression, then one of the others (for x if y else z operator)
  }

  void visit(ListConstructor* a) {}
  void visit(DictConstructor* a) {}
  void visit(SetConstructor* a) {}
  void visit(TupleConstructor* a) {}
  void visit(ListComprehension* a) {}
  void visit(DictComprehension* a) {}
  void visit(SetComprehension* a) {}
  void visit(LambdaDefinition* a) {}

  void visit(FunctionCall* a) {}
  void visit(ArrayIndex* a) {}
  void visit(ArraySlice* a) {}
  void visit(IntegerConstant* a) {}
  void visit(FloatingConstant* a) {}
  void visit(StringConstant* a) {}
  void visit(TrueConstant* a) {}
  void visit(FalseConstant* a) {}
  void visit(NoneConstant* a) {}
  void visit(VariableLookup* a) {}
  void visit(AttributeLookup* a) {}

  void visit(ModuleStatement* a) {
    // evaluate everything in order
  }
  void visit(ExpressionStatement* a) {
    // evaluate the expression
  }
  void visit(AssignmentStatement* a) {
    // evaluate the RHS, then put it on the unpacking stack and match it with the LHS to modify the environment
  }
  void visit(AugmentStatement* a) {
    // evaluate the RHSes, then match them with the LHSes
  }
  void visit(PrintStatement* a) {
    // evaluate the stream, then str() each expression on the RHS and call write() on the stream
    // also write "\n" if a->suppress_newline is false
  }
  void visit(DeleteStatement* a) {
    // unbind the given variables
  }
  void visit(PassStatement* a) {
    // do nothing
  }
  void visit(ImportStatement* a) {
    // load the source for the given module(s)
    // execute the modules and put them in module_env
    // bind variables appropriately:
    //   "import x, y": bind x, y to loaded module objects
    //   "import x, y as a, b": bind a, b to loaded module objects
    //   "from x import y, z": bind y, z in local scope to y, z in loaded module's env
    //   "from x import y, z as a, b": bind a, b in local scope to y, z in loaded module's env
    //   "from x, y import *": bind all variables in loaded modules' envs to same names in local env
  }
  void visit(GlobalStatement* a) {
    // do nothing. these were already handled by LValueCollectionVisitor
  }
  void visit(ExecStatement* a) {
    // lex and parse the value of the given string
    // then visit it
  }
  void visit(AssertStatement* a) {
    // evaluate the expression in a
    // if falsey, raise AssertionError
  }
  void visit(BreakStatement* a) {
    // set the break flag
  }
  void visit(ContinueStatement* a) {
    // set the continue flag
  }
  void visit(ReturnStatement* a) {
    // set the return value
  }
  void visit(RaiseStatement* a) {
    // raise an exception
  }
  void visit(YieldStatement* a) {
    // TODO
  }
  void visit(IfStatement* a) {
    // evaluate check; if truthy, execute compound
    // if falsey, visit the elif statements in order until one of them is truthy
    // if none are truthy, visit the else statement if present
  }
  void visit(ElifStatement* a) {}
  void visit(ElseStatement* a) {}
  void visit(ForStatement* a) {
    // evaluate source data; it had better be iterable
    // iterate it
    // if break_flag is false when done iterating, execute else statement if present
  }
  void visit(WhileStatement* a) {
    // evaluate condition; if true, execute compound
    // if break_flag is false and condition is false, execute else statement if present
  }
  void visit(TryStatement* a) {
    // execute the compound, but check for Exception after each step
    //   if it's done and there's no Exception, execute the else statement, checking for Exception after each step
    //     if there's an exception, don't match it to the excepts
    //   if there's an exception, try to match it to the Except clauses in order
    //     if it matches one, clear the current exception and execute it
    //   if it doesn't match, leave the current exception set
    // execute the finally statement
  }
  void visit(ExceptStatement* a) {}
  void visit(FinallyStatement* a) {}
  void visit(WithStatement* a) {
    // execute the constructors, bind them to the names
    // execute the compound
  }
  void visit(FunctionDefinition* a) {
    // bind a function object to its name in the local scope
  }
  void visit(ClassDefinition* a) {
    // bind a class object to its name in the local scope
  }
};



static void collect_variables(CompoundStatement* suite, Environment* env) {
  LValueCollectionVisitor v(env);
  suite->accept(&v);
  for (map<string, PyValue>::iterator it = env->names.begin(); it != env->names.end(); it++)
    printf("LOCAL: %s = %s\n", it->first.c_str(), it->second.str().c_str());
}

void import_module(string module_name, CompoundStatement* module, Environment* module_env) {
  collect_variables(module, &global->modules[module_name]);
  // TODO
}

void exec_tree(CompoundStatement* suite, Environment* env, Environment* module_env) {
  // TODO
}
