#include "CompilationVisitor.hh"

#include <inttypes.h>
#include <stdio.h>

#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>

#include "PythonLexer.hh"
#include "PythonParser.hh"
#include "PythonASTNodes.hh"
#include "PythonASTVisitor.hh"
#include "Environment.hh"
#include "AMD64Assembler.hh"

using namespace std;



// system v calling convention (linux, osx): these registers are callee-save
static const int64_t default_available_registers =
    (1 << Register::RAX) |
    (1 << Register::RCX) |
    (1 << Register::RDX) |
    (1 << Register::RSI) |
    (1 << Register::RDI) |
    (1 << Register::R8) |
    (1 << Register::R9) |
    (1 << Register::R10) |
    (1 << Register::R11);



CompilationVisitor::CompilationVisitor(GlobalAnalysis* global,
    ModuleAnalysis* module, int64_t target_function_id, int64_t target_split_id)
    : global(global), module(module),
    target_function(this->global->context_for_function(target_function_id)),
    target_split_id(target_split_id),
    available_registers(default_available_registers) { }

const string& CompilationVisitor::get_compiled_code() const {
  return this->compiled;
}



Register CompilationVisitor::reserve_register(Register which) {
  if (which == Register::None) {
    which = this->available_register();
  }
  if (!(this->available_registers & (1 << which))) {
    throw compile_error("register is not available");
  }
  this->available_registers &= ~(1 << which);
  return which;
}

void CompilationVisitor::release_register(Register which) {
  this->available_registers |= (1 << which);
}

Register CompilationVisitor::available_register() {
  Register which;
  for (which = Register::RAX; // = 0
       !(this->available_registers & (1 << which)) && (static_cast<int64_t>(which) < Register::Count);
       which = static_cast<Register>(static_cast<int64_t>(which) + 1));
  if (static_cast<int64_t>(which) >= Register::Count) {
    throw compile_error("no registers are available");
  }
  return which;
}



void CompilationVisitor::visit(UnaryOperation* a) {
  // generate code for the value expression
  a->expr->accept(this);

  if (this->current_type.type == ValueType::Indeterminate) {
    throw compile_error("operand has Indeterminate type", a->file_offset);
  }

  // now apply the unary operation on top of the result
  // we can use the same target register
  switch (a->oper) {

    case UnaryOperator::LogicalNot:
      if (this->current_type.type == ValueType::None) {
        // `not None` is always true
        this->compiled += generate_mov(this->target_register, 1,
            OperandSize::QuadWord);

      } else if (this->current_type.type == ValueType::Bool) {
        // bools are either 0 or 1; just flip it
        this->compiled += generate_xor(MemoryReference(this->target_register),
            1, OperandSize::QuadWord);

      } else if (this->current_type.type == ValueType::Int) {
        // check if the value is zero
        this->compiled += generate_test(MemoryReference(this->target_register),
            MemoryReference(this->target_register), OperandSize::QuadWord);
        this->compiled += generate_mov(this->target_register, 0,
            OperandSize::QuadWord);
        this->compiled += generate_setz(this->target_register);

      } else if (this->current_type == ValueType::Float) {
        // TODO
        throw compile_error("floating-point operations not yet supported", a->file_offset);

      } else if ((this->current_type == ValueType::Bytes) ||
                 (this->current_type == ValueType::Unicode) ||
                 (this->current_type == ValueType::List) ||
                 (this->current_type == ValueType::Tuple) ||
                 (this->current_type == ValueType::Set) ||
                 (this->current_type == ValueType::Dict)) {
        // load the size field, check if it's zero
        this->compiled += generate_mov(MemoryReference(this->target_register),
            MemoryReference(this->target_register, 0x08), OperandSize::QuadWord);
        this->compiled += generate_test(MemoryReference(this->target_register),
            MemoryReference(this->target_register), OperandSize::QuadWord);
        this->compiled += generate_mov(this->target_register, 0,
            OperandSize::QuadWord);
        this->compiled += generate_setz(this->target_register);

      } else {
        this->compiled += generate_mov(this->target_register, 1,
            OperandSize::QuadWord);
      }
      break;

    case UnaryOperator::Not:
      if ((this->current_type.type == ValueType::Int) ||
          (this->current_type.type == ValueType::Bool)) {
        this->compiled += generate_not(MemoryReference(this->target_register),
            OperandSize::QuadWord);
      } else {
        throw compile_error("bitwise not can only be applied to ints and bools", a->file_offset);
      }
      break;

    case UnaryOperator::Positive:
      if ((this->current_type.type == ValueType::Int) ||
          (this->current_type.type == ValueType::Bool) ||
          (this->current_type.type == ValueType::Float)) {
        throw compile_error("arithmetic positive can only be applied to numeric values", a->file_offset);
      }
      break;

    case UnaryOperator::Negative:
      if ((this->current_type.type == ValueType::Bool) ||
          (this->current_type.type == ValueType::Int)) {
        this->compiled += generate_not(MemoryReference(this->target_register),
            OperandSize::QuadWord);

      } else if (this->current_type == ValueType::Float) {
        // TODO: some stuff with fmul (there's no fneg)
        throw compile_error("floating-point operations not yet supported", a->file_offset);

      } else {
        throw compile_error("arithmetic negative can only be applied to numeric values", a->file_offset);
      }
      break;

    case UnaryOperator::Representation:
      throw compile_error("repr operator not supported; use repr() instead", a->file_offset);

    case UnaryOperator::Yield:
      throw compile_error("yield operator not yet supported", a->file_offset);
  }
}

bool CompilationVisitor::is_always_truthy(const Variable& type) {
  return (type.type == ValueType::Function) ||
         (type.type == ValueType::Class) ||
         (type.type == ValueType::Module);
}

bool CompilationVisitor::is_always_falsey(const Variable& type) {
  return (type.type == ValueType::None);
}

string CompilationVisitor::generate_truth_value_test(Register reg,
    const Variable& type, ssize_t file_offset) {

  switch (current_type.type) {
    case ValueType::Indeterminate:
      throw compile_error("truth value test on Indeterminate type", file_offset);

    case ValueType::Bool:
    case ValueType::Int:
      return generate_test(MemoryReference(reg), MemoryReference(reg),
          OperandSize::QuadWord);

    case ValueType::Float:
      throw compile_error("floating-point truth tests not yet implemented", file_offset);

    case ValueType::Bytes:
    case ValueType::Unicode:
    case ValueType::List:
    case ValueType::Tuple:
    case ValueType::Set:
    case ValueType::Dict: {
      // we have to use a register for this
      Register size_reg = this->reserve_register();
      string ret = generate_mov(MemoryReference(size_reg),
          MemoryReference(reg, 0x08), OperandSize::QuadWord);
      ret += generate_test(MemoryReference(size_reg), MemoryReference(size_reg),
          OperandSize::QuadWord);
      this->release_register(size_reg);
      return ret;
    }

    case ValueType::None:
    case ValueType::Function:
    case ValueType::Class:
    case ValueType::Module: {
      string type_str = type.str();
      throw compile_error(string_printf(
          "cannot generate truth test for %s value", type_str.c_str()),
          file_offset);
    }
  }
}

void CompilationVisitor::visit(BinaryOperation* a) {
  // LogicalOr and LogicalAnd may not evaluate the right-side operand, so we
  // have to implement those separately (the other operators evaluate both
  // operands in all cases)
  if ((a->oper == BinaryOperator::LogicalOr) ||
      (a->oper == BinaryOperator::LogicalAnd)) {
    // generate code for the right value and save it for later
    string right;
    this->compiled.swap(right);
    a->right->accept(this);
    this->compiled.swap(right);

    // generate code for the left value
    a->left->accept(this);

    // if the operator is trivialized, omit the right-side code
    if ((a->oper == BinaryOperator::LogicalOr) &&
        is_always_truthy(this->current_type)) {
      return;
    }
    if ((a->oper == BinaryOperator::LogicalAnd) &&
        is_always_falsey(this->current_type)) {
      return;
    }

    this->compiled += this->generate_truth_value_test(this->target_register,
        this->current_type, a->file_offset);
    this->compiled += generate_test(MemoryReference(this->target_register),
        MemoryReference(this->target_register), OperandSize::QuadWord);

    // for LogicalOr, use the left value if it's nonzero and use the right value
    // otherwise; for LogicalAnd, do the opposite
    if (a->oper == BinaryOperator::LogicalOr) {
      this->compiled += generate_jnz(right.size()); // skip right if left != zero
    } else { // LogicalAnd
      this->compiled += generate_jz(right.size()); // skip right if left == zero
    }
    this->compiled += right;
    return;
  }

  // all of the remaining operators use both operands, so evaluate both of them
  // into different registers
  Register left_reg = this->target_register;
  a->left->accept(this);
  Register right_reg = this->reserve_register();
  this->target_register = right_reg;
  a->right->accept(this);
  this->target_register = left_reg;

  switch (a->oper) {
    case BinaryOperator::LessThan:
      // TODO
      throw compile_error("operator not yet implemented", a->file_offset);
    case BinaryOperator::GreaterThan:
      // TODO
      throw compile_error("operator not yet implemented", a->file_offset);
    case BinaryOperator::Equality:
      // TODO
      throw compile_error("operator not yet implemented", a->file_offset);
    case BinaryOperator::GreaterOrEqual:
      // TODO
      throw compile_error("operator not yet implemented", a->file_offset);
    case BinaryOperator::LessOrEqual:
      // TODO
      throw compile_error("operator not yet implemented", a->file_offset);
    case BinaryOperator::NotEqual:
      // TODO
      throw compile_error("operator not yet implemented", a->file_offset);
    case BinaryOperator::In:
      // TODO
      throw compile_error("operator not yet implemented", a->file_offset);
    case BinaryOperator::NotIn:
      // TODO
      throw compile_error("operator not yet implemented", a->file_offset);
    case BinaryOperator::Is:
      // TODO
      throw compile_error("operator not yet implemented", a->file_offset);
    case BinaryOperator::IsNot:
      // TODO
      throw compile_error("operator not yet implemented", a->file_offset);
    case BinaryOperator::Or:
      // TODO
      throw compile_error("operator not yet implemented", a->file_offset);
    case BinaryOperator::And:
      // TODO
      throw compile_error("operator not yet implemented", a->file_offset);
    case BinaryOperator::Xor:
      // TODO
      throw compile_error("operator not yet implemented", a->file_offset);
    case BinaryOperator::LeftShift:
      // TODO
      throw compile_error("operator not yet implemented", a->file_offset);
    case BinaryOperator::RightShift:
      // TODO
      throw compile_error("operator not yet implemented", a->file_offset);
    case BinaryOperator::Addition:
      // TODO
      throw compile_error("operator not yet implemented", a->file_offset);
    case BinaryOperator::Subtraction:
      // TODO
      throw compile_error("operator not yet implemented", a->file_offset);
    case BinaryOperator::Multiplication:
      // TODO
      throw compile_error("operator not yet implemented", a->file_offset);
    case BinaryOperator::Division:
      // TODO
      throw compile_error("operator not yet implemented", a->file_offset);
    case BinaryOperator::Modulus:
      // TODO
      throw compile_error("operator not yet implemented", a->file_offset);
    case BinaryOperator::IntegerDivision:
      // TODO
      throw compile_error("operator not yet implemented", a->file_offset);
    case BinaryOperator::Exponentiation:
      // TODO
      throw compile_error("operator not yet implemented", a->file_offset);
    default:
      throw compile_error("unhandled binary operator", a->file_offset);
  }
  this->release_register(right_reg);
}

void CompilationVisitor::visit(TernaryOperation* a) {
  if (a->oper != TernaryOperator::IfElse) {
    throw compile_error("unrecognized ternary operator", a->file_offset);
  }

  // generate both branches
  string left, right;
  this->compiled.swap(right); // put compiled into right for now, generate left
  a->left->accept(this);
  Variable left_type = this->current_type;
  this->compiled.swap(left); // put left in place, make compiled blank again
  a->right->accept(this);
  this->compiled.swap(right); // put right in place, move compiled into place

  // TODO: support different value types (maybe by splitting the function)
  if (left_type != this->current_type) {
    throw compile_error("sides have different types", a->file_offset);
  }

  // left comes first in the code, so it will need a jmp at the end
  left += generate_jmp(right.size()); // jump over all of right

  // generate code for the condition value
  a->center->accept(this);

  // test the condition value. left is the value if true, so we jump to right if
  // the value is zero
  this->compiled += generate_test(MemoryReference(this->target_register),
      MemoryReference(this->target_register), OperandSize::QuadWord);
  this->compiled += generate_jz(left.size());
  this->compiled += left;
  this->compiled += right;
}

void CompilationVisitor::visit(ListConstructor* a) {
  // TODO: generate malloc call, item constructor code
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(SetConstructor* a) {
  // TODO: generate malloc call, item constructor and insert code
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(DictConstructor* a) {
  // TODO: generate malloc call, item constructor and insert code
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(TupleConstructor* a) {
  // TODO: generate malloc call, item constructor code
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(ListComprehension* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(SetComprehension* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(DictComprehension* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(LambdaDefinition* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

static const vector<Register> argument_register_order = {
  Register::RDI, Register::RSI, Register::RCX, Register::R8, Register::R9,
};

void CompilationVisitor::visit(FunctionCall* a) {
  // system v calling convention:
  // int arguments in rdi, rsi, rcx, r8, r9 (in that order); more ints on stack
  // float arguments in xmm0-7; more floats on stack
  // some special handling for variadic functions (need to look this up)
  // return in rax, rdx

  // TODO: we should support dynamic function references (e.g. looking them up
  // in dicts and whatnot)
  if (a->callee_function_id == 0) {
    throw compile_error("can\'t resolve function reference", a->file_offset);
  }

  auto* callee_context = this->global->context_for_function(a->callee_function_id);
  if (a->callee_function_id == 0) {
    throw compile_error(string_printf("function %" PRId64 " has no context object", a->callee_function_id),
        a->file_offset);
  }

  // order of arguments:
  // 1. positional arguments, in the order defined in the function
  // 2. keyword arguments, in the order defined in the function
  // 3. variadic arguments
  // we currently don't support variadic keyword arguments at all

  // TODO: figure out how to support variadic arguments
  if (a->varargs.get() || a->varkwargs.get()) {
    throw compile_error("variadic function calls not supported", a->file_offset);
  }
  if (!callee_context->varargs_name.empty() || !callee_context->varkwargs_name.empty()) {
    throw compile_error("variadic function definitions not supported", a->file_offset);
  }

  // python evaluates arguments in left-to-right order, so we reserve all the
  // stack space we'll need first, then write them in the appropriate locations
  size_t arg_stack_bytes = (a->args.size() > argument_register_order.size()) ?
      ((a->args.size() - argument_register_order.size()) * sizeof(int64_t)) : 0;
  if (arg_stack_bytes) {
    this->compiled += generate_sub(MemoryReference(Register::RSP),
        arg_stack_bytes, OperandSize::QuadWord);
  }

  // separate out the positional and keyword args from the call args
  const vector<shared_ptr<Expression>>& positional_call_args = a->args;
  const unordered_map<string, shared_ptr<Expression>> keyword_call_args = a->kwargs;

  // push positional args first
  vector<Variable> arg_type;
  size_t arg_index = 0;
  for (; arg_index < positional_call_args.size(); arg_index++) {
    if (arg_index >= callee_context->args.size()) {
      throw compile_error("too many arguments in function call", a->file_offset);
    }

    const auto& callee_arg = callee_context->args[arg_index];
    const auto& call_arg = positional_call_args[arg_index];

    // if the callee arg is a kwarg and the call also includes that kwarg, fail
    if ((callee_arg.default_value.type != ValueType::Indeterminate) &&
        keyword_call_args.count(callee_arg.name)) {
      throw compile_error(string_printf("argument %s specified multiple times",
          callee_arg.name.c_str()), a->file_offset);
    }

    // generate code for this argument
    this->generate_code_for_call_arg(call_arg, arg_index);
    arg_type.emplace_back(move(this->current_type));
  }

  // now push keyword args, in the order the function defines them
  for (; arg_index < callee_context->args.size(); arg_index++) {
    const auto& callee_arg = callee_context->args[arg_index];

    try {
      const auto& call_arg = keyword_call_args.at(callee_arg.name);
      this->generate_code_for_call_arg(call_arg, arg_index);

    } catch (const out_of_range& e) {
      this->generate_code_for_call_arg(callee_arg.default_value, arg_index);
    }
    arg_type.emplace_back(move(this->current_type));
  }

  // at this point we should have the same number of args overall as the
  // function wants
  if (arg_type.size() != callee_context->args.size()) {
    throw compile_error("incorrect argument count in function call", a->file_offset);
  }

  // pick a register to clobber with the function address (it's important that
  // we do this before unreserving the argument registers below, or we might
  // clobber one of those)
  Register call_address_register = this->available_register();

  // we can now unreserve the argument registers
  for (size_t arg_index = 0;
       (arg_index < argument_register_order.size()) &&
         (arg_index < arg_type.size());
       arg_index++) {
    this->release_register(argument_register_order[arg_index]);
  }

  // any remaining reserved registers will need to be saved
  // TODO: for now, just assert that there are no reserved registers (I'm lazy)
  if (this->available_registers != default_available_registers) {
    throw compile_error(string_printf("some registers were reserved at function call (%" PRIX64 " available, %" PRIX64 " expected)",
        this->available_registers, default_available_registers), a->file_offset);
  }

  // figure out which fragment to call. if a fragment exists that has this
  // signature, use it; otherwise, generate a call to the compiler instead
  string arg_signature = type_signature_for_variables(arg_type);
  try {
    int64_t fragment_id = callee_context->arg_signature_to_fragment_id.at(arg_signature);
    const auto& fragment = callee_context->fragments.at(fragment_id);
    int64_t call_address = reinterpret_cast<int64_t>(fragment.compiled);
    this->compiled += generate_mov(call_address_register, call_address,
        OperandSize::QuadWord);
    this->compiled += generate_call(MemoryReference(call_address_register));

  } catch (const std::out_of_range& e) {
    // the fragment doesn't exist. we won't build it just yet; we'll generate a
    // call to the compiler that will build it later
    throw compile_error("referenced fragment does not exist", a->file_offset);
  }

  // now just pop any arg space off the stack, and we're done
  if (arg_stack_bytes) {
    this->compiled += generate_add(MemoryReference(Register::RSP),
        arg_stack_bytes, OperandSize::QuadWord);
  }
}

void CompilationVisitor::visit(ArrayIndex* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(ArraySlice* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(IntegerConstant* a) {
  this->compiled += generate_mov(this->target_register, a->value,
      OperandSize::QuadWord);
  this->current_type = Variable(ValueType::Int);
}

void CompilationVisitor::visit(FloatConstant* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(BytesConstant* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(UnicodeConstant* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(TrueConstant* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(FalseConstant* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(NoneConstant* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(VariableLookup* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(AttributeLookup* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(TupleLValueReference* a) {
  // TODO
  throw compile_error("TupleLValueReference not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(ArrayIndexLValueReference* a) {
  // TODO
  throw compile_error("ArrayIndexLValueReference not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(ArraySliceLValueReference* a) {
  // TODO
  throw compile_error("ArraySliceLValueReference not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(AttributeLValueReference* a) {
  if (a->base.get()) {
    throw compile_error("AttributeLValueReference with nontrivial base not yet implemented", a->file_offset);
  }

  // if we're writing a global, use its global slot offset
  if (!this->target_function || this->target_function->globals.count(a->name)) {
    ssize_t offset = distance(this->module->globals.begin(),
        this->module->globals.find(a->name));
    this->lvalue_target.offset = offset * sizeof(int64_t);
    this->lvalue_target.is_global = true;

  // if we're writing a local, use its local slot offset
  } else {
    ssize_t offset = distance(this->target_function->locals.begin(),
        this->target_function->locals.find(a->name));
    this->lvalue_target.offset = offset * sizeof(int64_t);
    this->lvalue_target.is_global = false;
  }
}

// statement visitation
void CompilationVisitor::visit(ModuleStatement* a) {
  // just visit all the statements
  this->RecursiveASTVisitor::visit(a);
}

void CompilationVisitor::visit(ExpressionStatement* a) {
  // just evaluate it into some random register
  this->target_register = this->available_register();
  a->expr->accept(this);
}

void CompilationVisitor::visit(AssignmentStatement* a) {
  // unlike in AnalysisVisitor, we look at the lvalue references first, so we
  // can know where to put the resulting values when generating their code

  // TODO: currently we don't support unpacking at all; we only support simple
  // assignments

  // this generates assignment_target
  a->target->accept(this);

  // generate code to load the value into any available register
  this->target_register = available_register();
  a->value->accept(this);

  // now save the value to the appropriate slot. global slots are offsets from
  // R13; local slots are offsets from RSP
  if (this->lvalue_target.is_global) {
    this->compiled += generate_mov(
        MemoryReference(Register::R13, this->module->global_base_offset + this->lvalue_target.offset),
        MemoryReference(this->target_register), OperandSize::QuadWord);
  } else {
    this->compiled += generate_mov(
        MemoryReference(Register::RBP, this->lvalue_target.offset),
        MemoryReference(this->target_register), OperandSize::QuadWord);
  }
}

void CompilationVisitor::visit(AugmentStatement* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(DeleteStatement* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(ImportStatement* a) {
  // we actually don't have to do anything here! all imports are done
  // statically, so the names already exist in the current scope and are linked
  // to the right objects
}

void CompilationVisitor::visit(GlobalStatement* a) {
  // nothing to do here; AnnotationVisitor already extracted all useful info
}

void CompilationVisitor::visit(ExecStatement* a) {
  // we don't support this
  throw compile_error("ExecStatement is not supported", a->file_offset);
}

void CompilationVisitor::visit(AssertStatement* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(BreakStatement* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(ContinueStatement* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(ReturnStatement* a) {
  // the value should be returned in rax
  this->target_register = Register::RAX;
  a->value->accept(this);
  // TODO: generate return or jmp opcodes appropriately
  throw compile_error("node type not completely implemented", a->file_offset);
}

void CompilationVisitor::visit(RaiseStatement* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(YieldStatement* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(SingleIfStatement* a) {
  throw logic_error("SingleIfStatement used instead of subclass");
}

void CompilationVisitor::visit(IfStatement* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(ElseStatement* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(ElifStatement* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(ForStatement* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(WhileStatement* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(ExceptStatement* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(FinallyStatement* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(TryStatement* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(WithStatement* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(FunctionDefinition* a) {
  // if this definition is not the function being compiled, don't recur; instead
  // treat it as an assignment (of the function context to the local/global var)
  if (!this->target_function ||
      (this->target_function->id != a->function_id)) {
    int64_t context = reinterpret_cast<int64_t>(
        this->global->context_for_function(a->function_id));
    if (!this->target_function) {
      // TODO: globals should be static (and prepopulated) in the future so we
      // don't have to generate code that just writes them once at import time
      ssize_t target_offset = static_cast<ssize_t>(distance(
          this->module->globals.begin(), this->module->globals.find(a->name))) * sizeof(int64_t);
      this->compiled += generate_mov(this->target_register, context,
          OperandSize::QuadWord);
      this->compiled += generate_mov(
          MemoryReference(Register::R13, this->module->global_base_offset + target_offset),
          MemoryReference(this->target_register), OperandSize::QuadWord);

    } else {
      ssize_t target_offset = -static_cast<ssize_t>(distance(
          this->target_function->locals.begin(),
          this->target_function->locals.find(a->name))) * sizeof(int64_t);
      // TODO: use `mov [r+off], val` (not yet implemented in the assembler)
      this->compiled += generate_mov(this->target_register, context,
          OperandSize::QuadWord);
      this->compiled += generate_mov(
          MemoryReference(Register::RBP, target_offset),
          MemoryReference(this->target_register), OperandSize::QuadWord);
    }
    return;
  }

  // TODO: we should generate lead-in and lead-out code here, then recur
  throw compile_error("FunctionDefinition not yet implemented", a->file_offset);
}

void CompilationVisitor::visit(ClassDefinition* a) {
  // TODO
  throw compile_error("node type not yet implemented", a->file_offset);
}

void CompilationVisitor::generate_code_for_call_arg(
    shared_ptr<Expression> value, size_t arg_index) {
  if (arg_index < argument_register_order.size()) {
    // construct it directly into the register, and leave the register reserved
    this->target_register = this->reserve_register(argument_register_order[arg_index]);
    value->accept(this);
  } else {
    // construct it into any register, then generate code to put it on the stack
    this->target_register = this->available_register();
    value->accept(this);
    this->compiled += generate_push(this->target_register);
  }
}

void CompilationVisitor::generate_code_for_call_arg(const Variable& value,
    size_t arg_index) {
  if (!value.value_known) {
    throw compile_error("can\'t generate code for unknown value");
  }

  // TODO
  throw compile_error("default values not yet implemented");
}
