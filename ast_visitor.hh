#ifndef _AST_VISITOR_HH
#define _AST_VISITOR_HH

struct ASTVisitor {

  template <typename T> void visit_list(vector<shared_ptr<T> >& list);

  // expression visitation
  virtual void visit(UnpackingTuple*);
  virtual void visit(UnpackingVariable*);
  virtual void visit(ArgumentDefinition*);
  virtual void visit(UnaryOperation*);
  virtual void visit(BinaryOperation*);
  virtual void visit(TernaryOperation*);
  virtual void visit(ListConstructor*);
  virtual void visit(DictConstructor*);
  virtual void visit(SetConstructor*);
  virtual void visit(TupleConstructor*);
  virtual void visit(ListComprehension*);
  virtual void visit(DictComprehension*);
  virtual void visit(SetComprehension*);
  virtual void visit(LambdaDefinition*);
  virtual void visit(FunctionCall*);
  virtual void visit(ArrayIndex*);
  virtual void visit(ArraySlice*);
  virtual void visit(IntegerConstant*);
  virtual void visit(FloatingConstant*);
  virtual void visit(StringConstant*);
  virtual void visit(TrueConstant*);
  virtual void visit(FalseConstant*);
  virtual void visit(NoneConstant*);
  virtual void visit(VariableLookup*);
  virtual void visit(AttributeLookup*);

  // statement visitation
  virtual void visit(ModuleStatement*);
  virtual void visit(ExpressionStatement*);
  virtual void visit(AssignmentStatement*);
  virtual void visit(AugmentStatement*);
  virtual void visit(PrintStatement*);
  virtual void visit(DeleteStatement*);
  virtual void visit(PassStatement*);
  virtual void visit(ImportStatement*);
  virtual void visit(GlobalStatement*);
  virtual void visit(ExecStatement*);
  virtual void visit(AssertStatement*);
  virtual void visit(BreakStatement*);
  virtual void visit(ContinueStatement*);
  virtual void visit(ReturnStatement*);
  virtual void visit(RaiseStatement*);
  virtual void visit(YieldStatement*);
  virtual void visit(SingleIfStatement*);
  virtual void visit(ElseStatement*);
  virtual void visit(IfStatement*);
  virtual void visit(ElifStatement*);
  virtual void visit(ForStatement*);
  virtual void visit(WhileStatement*);
  virtual void visit(ExceptStatement*);
  virtual void visit(FinallyStatement*);
  virtual void visit(TryStatement*);
  virtual void visit(WithStatement*);
  virtual void visit(FunctionDefinition*);
  virtual void visit(ClassDefinition*);
};

#endif // _AST_VISITOR_HH
