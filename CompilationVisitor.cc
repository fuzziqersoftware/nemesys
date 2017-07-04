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
#include "BuiltinTypes.hh"

using namespace std;



// in general we follow the system v calling convention:
// - int arguments in rdi, rsi, rdx, rcx, r8, r9 (in that order); more on stack
// - float arguments in xmm0-7; more floats on stack
// - some special handling for variadic functions (need to look this up)
// - return in rax, rdx
//
// space for all local variables is reserved at the beginning of the function's
// scope, but not initialized. temporary variables are pushed onto the stack
// and popped during the function's execution; this means stack offsets might be
// larger than expected during statement compilation. temporary variables may
// only live during a statement's execution; when a statement is completed, they
// are either copied to a local/global variable or destroyed.
//
// the nemesys calling convention is a little more complex than the system v
// convention, but is fully compatible with it, so nemesys functions can
// directly call c functions (e.g. built-in functions in nemesys itself). the
// nemesys register assignment is as follows:
//
// reg =  save  = purpose
// RAX = caller = function return values
// RCX = caller = 4th int arg
// RDX = caller = 3rd int arg
// RBX = callee = unused
// RSP =        = stack
// RBP = callee = stack frame pointer
// RSI = caller = 2nd int arg
// RDI = caller = 1st int arg
// R8  = caller = 5th int arg
// R9  = caller = 6th int arg
// R10 = caller = temp values
// R11 = caller = temp values
// R12 = callee = unused
// R13 = callee = global space pointer
// R14 = callee = built-in function array pointer
// R15 = callee = unused
//
// globals are referenced by offsets from r13. each module has a
// statically-assigned space above r13, and should read/write globals with
// `mov [r13 + X]` opcodes. module root scopes compile into functions that
// return nothing and take the global pointer as an argument. functions defined
// within modules expect the global pointer to already be in r13.

// TODO: keep common function references around somewhere (r14 maybe) and use
// `call [r14 + X]` instead of `mov rax, X; call rax`.

static const vector<Register> argument_register_order = {
  Register::RDI, Register::RSI, Register::RDX, Register::RCX, Register::R8,
  Register::R9,
};

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
    : file_offset(-1), global(global), module(module),
    target_function(this->global->context_for_function(target_function_id)),
    target_split_id(target_split_id),
    available_registers(default_available_registers), stack_bytes_used(0) { }

AMD64Assembler& CompilationVisitor::assembler() {
  return this->as;
}



Register CompilationVisitor::reserve_register(Register which) {
  if (which == Register::None) {
    which = this->available_register();
  }
  if (!(this->available_registers & (1 << which))) {
    throw compile_error("register is not available", this->file_offset);
  }
  this->available_registers &= ~(1 << which);
  return which;
}

void CompilationVisitor::release_register(Register which) {
  this->available_registers |= (1 << which);
}

Register CompilationVisitor::available_register(Register preferred) {
  if (this->available_registers & (1 << preferred)) {
    return preferred;
  }

  Register which;
  for (which = Register::RAX; // = 0
       !(this->available_registers & (1 << which)) && (static_cast<int64_t>(which) < Register::Count);
       which = static_cast<Register>(static_cast<int64_t>(which) + 1));
  if (static_cast<int64_t>(which) >= Register::Count) {
    throw compile_error("no registers are available", this->file_offset);
  }
  return which;
}

int64_t CompilationVisitor::write_push_reserved_registers() {
  Register which;
  for (which = Register::RAX; // = 0
      static_cast<int64_t>(which) < Register::Count;
       which = static_cast<Register>(static_cast<int64_t>(which) + 1)) {
    if (!(default_available_registers & (1 << which))) {
      continue; // this register isn't used by CompilationVisitor
    }
    if (this->available_registers & (1 << which)) {
      continue; // this register is used but not reserved
    }

    this->write_push(which);
  }

  int64_t ret = this->available_registers;
  this->available_registers = default_available_registers;
  return ret;
}

void CompilationVisitor::write_pop_reserved_registers(int64_t mask) {
  if (this->available_registers != default_available_registers) {
    throw compile_error("some registers were not released when reserved were popped", this->file_offset);
  }

  Register which;
  for (which = Register::R15; // = 0
      static_cast<int64_t>(which) > Register::None;
       which = static_cast<Register>(static_cast<int64_t>(which) - 1)) {
    if (!(default_available_registers & (1 << which))) {
      continue; // this register isn't used by CompilationVisitor
    }
    if (mask & (1 << which)) {
      continue; // this register is used but not reserved
    }

    this->write_pop(which);
  }

  this->available_registers = mask;
}



void CompilationVisitor::visit(UnaryOperation* a) {
  this->file_offset = a->file_offset;

  this->as.write_label(string_printf("__UnaryOperation_%p_evaluate", a));

  // generate code for the value expression
  a->expr->accept(this);

  if (this->current_type.type == ValueType::Indeterminate) {
    throw compile_error("operand has Indeterminate type", this->file_offset);
  }

  // now apply the unary operation on top of the result
  // we can use the same target register
  this->as.write_label(string_printf("__UnaryOperation_%p_apply", a));
  switch (a->oper) {

    case UnaryOperator::LogicalNot:
      if (this->current_type.type == ValueType::None) {
        // `not None` is always true
        this->as.write_mov(this->target_register, 1);

      } else if (this->current_type.type == ValueType::Bool) {
        // bools are either 0 or 1; just flip it
        this->as.write_xor(MemoryReference(this->target_register), 1);

      } else if (this->current_type.type == ValueType::Int) {
        // check if the value is zero
        this->as.write_test(MemoryReference(this->target_register),
            MemoryReference(this->target_register));
        this->as.write_mov(this->target_register, 0);
        this->as.write_setz(this->target_register);

      } else if (this->current_type == ValueType::Float) {
        // TODO
        throw compile_error("floating-point operations not yet supported", this->file_offset);

      } else if ((this->current_type == ValueType::Bytes) ||
                 (this->current_type == ValueType::Unicode) ||
                 (this->current_type == ValueType::List) ||
                 (this->current_type == ValueType::Tuple) ||
                 (this->current_type == ValueType::Set) ||
                 (this->current_type == ValueType::Dict)) {
        // load the size field, check if it's zero
        this->as.write_mov(MemoryReference(this->target_register),
            MemoryReference(this->target_register, 0x08));
        this->as.write_test(MemoryReference(this->target_register),
            MemoryReference(this->target_register));
        this->as.write_mov(this->target_register, 0);
        this->as.write_setz(this->target_register);

      } else {
        // other types cannot be falsey
        this->as.write_mov(this->target_register, 1);
      }

      this->write_destructor_call(this->target_register, this->current_type);
      this->current_type = Variable(ValueType::Bool);
      break;

    case UnaryOperator::Not:
      if ((this->current_type.type == ValueType::Int) ||
          (this->current_type.type == ValueType::Bool)) {
        this->as.write_not(MemoryReference(this->target_register));
      } else {
        throw compile_error("bitwise not can only be applied to ints and bools", this->file_offset);
      }
      this->current_type = Variable(ValueType::Int);
      break;

    case UnaryOperator::Positive:
      if ((this->current_type.type == ValueType::Int) ||
          (this->current_type.type == ValueType::Bool) ||
          (this->current_type.type == ValueType::Float)) {
        throw compile_error("arithmetic positive can only be applied to numeric values", this->file_offset);
      }
      break;

    case UnaryOperator::Negative:
      if ((this->current_type.type == ValueType::Bool) ||
          (this->current_type.type == ValueType::Int)) {
        this->as.write_not(MemoryReference(this->target_register));

      } else if (this->current_type == ValueType::Float) {
        // TODO: some stuff with fmul (there's no fneg)
        throw compile_error("floating-point operations not yet supported", this->file_offset);

      } else {
        throw compile_error("arithmetic negative can only be applied to numeric values", this->file_offset);
      }
      break;

    case UnaryOperator::Yield:
      throw compile_error("yield operator not yet supported", this->file_offset);
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

void CompilationVisitor::write_truth_value_test(Register reg,
    const Variable& type) {

  switch (current_type.type) {
    case ValueType::Indeterminate:
      throw compile_error("truth value test on Indeterminate type", this->file_offset);

    case ValueType::Bool:
    case ValueType::Int:
      this->as.write_test(MemoryReference(reg), MemoryReference(reg));

    case ValueType::Float:
      throw compile_error("floating-point truth tests not yet implemented", this->file_offset);

    case ValueType::Bytes:
    case ValueType::Unicode:
    case ValueType::List:
    case ValueType::Tuple:
    case ValueType::Set:
    case ValueType::Dict: {
      // we have to use a register for this
      Register size_reg = this->available_register();
      this->as.write_mov(MemoryReference(size_reg), MemoryReference(reg, 0x08));
      this->as.write_test(MemoryReference(size_reg), MemoryReference(size_reg));
    }

    case ValueType::None:
    case ValueType::Function:
    case ValueType::Class:
    case ValueType::Module: {
      string type_str = type.str();
      throw compile_error(string_printf(
          "cannot generate truth test for %s value", type_str.c_str()),
          this->file_offset);
    }
  }
}

void CompilationVisitor::visit(BinaryOperation* a) {
  this->file_offset = a->file_offset;

  // LogicalOr and LogicalAnd may not evaluate the right-side operand, so we
  // have to implement those separately (the other operators evaluate both
  // operands in all cases)
  if ((a->oper == BinaryOperator::LogicalOr) ||
      (a->oper == BinaryOperator::LogicalAnd)) {
    // generate code for the left value
    this->as.write_label(string_printf("__BinaryOperation_%p_evaluate_left", a));
    a->left->accept(this);

    // if the operator is trivialized, omit the right-side code
    if ((a->oper == BinaryOperator::LogicalOr) &&
        is_always_truthy(this->current_type)) {
      this->as.write_label(string_printf("__BinaryOperation_%p_trivialized_true", a));
      return;
    }
    if ((a->oper == BinaryOperator::LogicalAnd) &&
        is_always_falsey(this->current_type)) {
      this->as.write_label(string_printf("__BinaryOperation_%p_trivialized_false", a));
      return;
    }

    Variable left_type = move(this->current_type);

    // for LogicalOr, use the left value if it's nonzero and use the right value
    // otherwise; for LogicalAnd, do the opposite
    string label_name = string_printf("BinaryOperation_%p_evaluate_right", a);
    this->write_truth_value_test(this->target_register, this->current_type);
    if (a->oper == BinaryOperator::LogicalOr) {
      this->as.write_jnz(label_name); // skip right if left truthy
    } else { // LogicalAnd
      this->as.write_jz(label_name); // skip right if left falsey
    }

    // generate code for the right value
    a->right->accept(this);
    this->as.write_label(label_name);

    if (left_type != this->current_type) {
      throw compile_error("logical combine operator has different return types", this->file_offset);
    }
    return;
  }

  // all of the remaining operators use both operands, so evaluate both of them
  // into different registers
  this->as.write_label(string_printf("__BinaryOperation_%p_evaluate_left", a));
  a->left->accept(this);
  this->write_push(this->target_register); // so right doesn't clobber it
  Variable left_type = move(this->current_type);

  this->as.write_label(string_printf("__BinaryOperation_%p_evaluate_right", a));
  a->right->accept(this);
  this->write_push(this->target_register); // for the destructor call later
  Variable& right_type = this->current_type;

  this->as.write_label(string_printf("__BinaryOperation_%p_combine", a));
  switch (a->oper) {
    case BinaryOperator::LessThan:
      // TODO
      throw compile_error("LessThan not yet implemented", this->file_offset);

    case BinaryOperator::GreaterThan:
      // TODO
      throw compile_error("GreaterThan not yet implemented", this->file_offset);
    case BinaryOperator::Equality:
      // TODO
      throw compile_error("Equality not yet implemented", this->file_offset);
    case BinaryOperator::GreaterOrEqual:
      // TODO
      throw compile_error("GreaterOrEqual not yet implemented", this->file_offset);
    case BinaryOperator::LessOrEqual:
      // TODO
      throw compile_error("LessOrEqual not yet implemented", this->file_offset);
    case BinaryOperator::NotEqual:
      // TODO
      throw compile_error("NotEqual not yet implemented", this->file_offset);
    case BinaryOperator::In:
      // TODO
      throw compile_error("In not yet implemented", this->file_offset);
    case BinaryOperator::NotIn:
      // TODO
      throw compile_error("NotIn not yet implemented", this->file_offset);
    case BinaryOperator::Is:
      // TODO
      throw compile_error("Is not yet implemented", this->file_offset);
    case BinaryOperator::IsNot:
      // TODO
      throw compile_error("IsNot not yet implemented", this->file_offset);
    case BinaryOperator::Or:
      // TODO
      throw compile_error("Or not yet implemented", this->file_offset);
    case BinaryOperator::And:
      // TODO
      throw compile_error("And not yet implemented", this->file_offset);
    case BinaryOperator::Xor:
      // TODO
      throw compile_error("Xor not yet implemented", this->file_offset);
    case BinaryOperator::LeftShift:
      // TODO
      throw compile_error("LeftShift not yet implemented", this->file_offset);
    case BinaryOperator::RightShift:
      // TODO
      throw compile_error("RightShift not yet implemented", this->file_offset);
    case BinaryOperator::Addition:
      if (((left_type.type == ValueType::Bytes) &&
           (right_type.type == ValueType::Bytes)) ||
          ((left_type.type == ValueType::Unicode) &&
           (right_type.type == ValueType::Unicode))) {
        this->write_function_call(reinterpret_cast<const void*>(&unicode_concat),
            {MemoryReference(Register::RSP, 8),
              MemoryReference(this->target_register)}, -1,
            this->target_register);
        break;
      }

      // TODO
      throw compile_error("Addition not yet implemented", this->file_offset);
    case BinaryOperator::Subtraction:
      // TODO
      throw compile_error("Subtraction not yet implemented", this->file_offset);
    case BinaryOperator::Multiplication:
      // TODO
      throw compile_error("Multiplication not yet implemented", this->file_offset);
    case BinaryOperator::Division:
      // TODO
      throw compile_error("Division not yet implemented", this->file_offset);
    case BinaryOperator::Modulus:
      // TODO
      throw compile_error("Modulus not yet implemented", this->file_offset);
    case BinaryOperator::IntegerDivision:
      // TODO
      throw compile_error("IntegerDivision not yet implemented", this->file_offset);
    case BinaryOperator::Exponentiation:
      // TODO
      throw compile_error("Exponentiation not yet implemented", this->file_offset);
    default:
      throw compile_error("unhandled binary operator", this->file_offset);
  }

  // save the return value before destroying the temp values
  this->write_push(this->target_register);

  // destroy the temp values
  this->as.write_label(string_printf("__BinaryOperation_%p_destroy_left", a));
  this->write_destructor_call(MemoryReference(Register::RSP, 8), left_type);
  this->as.write_label(string_printf("__BinaryOperation_%p_destroy_right", a));
  this->write_destructor_call(MemoryReference(Register::RSP, 16), right_type);

  // now load the result again and clean up the stack
  this->as.write_mov(MemoryReference(this->target_register),
      MemoryReference(Register::RSP, 0));
  this->adjust_stack(0x18);
}

void CompilationVisitor::visit(TernaryOperation* a) {
  this->file_offset = a->file_offset;

  if (a->oper != TernaryOperator::IfElse) {
    throw compile_error("unrecognized ternary operator", this->file_offset);
  }

  this->as.write_label(string_printf("__TernaryOperation_%p_evaluate", a));

  // generate the condition evaluation
  a->center->accept(this);

  // if the value is always truthy or always falsey, don't bother generating the
  // unused side
  if (this->is_always_truthy(this->current_type)) {
    a->left->accept(this);
    return;
  }
  if (this->is_always_falsey(this->current_type)) {
    a->right->accept(this);
    return;
  }

  // left comes first in the code
  string false_label = string_printf("TernaryOperation_%p_condition_false", a);
  string end_label = string_printf("TernaryOperation_%p_end", a);
  this->write_truth_value_test(this->target_register, this->current_type);
  this->as.write_jz(false_label); // skip left

  // generate code for the left (True) value
  a->left->accept(this);
  this->as.write_jmp(end_label);
  Variable left_type = move(this->current_type);

  // generate code for the right (False) value
  this->as.write_label(false_label);
  a->right->accept(this);
  this->as.write_label(end_label);

  // TODO: support different value types (maybe by splitting the function)
  if (left_type != this->current_type) {
    throw compile_error("sides have different types", this->file_offset);
  }
}

void CompilationVisitor::visit(ListConstructor* a) {
  this->file_offset = a->file_offset;

  // TODO: generate malloc call, item constructor code
  throw compile_error("ListConstructor not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(SetConstructor* a) {
  this->file_offset = a->file_offset;

  // TODO: generate malloc call, item constructor and insert code
  throw compile_error("SetConstructor not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(DictConstructor* a) {
  this->file_offset = a->file_offset;

  // TODO: generate malloc call, item constructor and insert code
  throw compile_error("DictConstructor not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(TupleConstructor* a) {
  this->file_offset = a->file_offset;

  // TODO: generate malloc call, item constructor code
  throw compile_error("TupleConstructor not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(ListComprehension* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("ListComprehension not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(SetComprehension* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("SetComprehension not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(DictComprehension* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("DictComprehension not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(LambdaDefinition* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("LambdaDefinition not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(FunctionCall* a) {
  this->file_offset = a->file_offset;

  // TODO: we should support dynamic function references (e.g. looking them up
  // in dicts and whatnot)
  if (a->callee_function_id == 0) {
    throw compile_error("can\'t resolve function reference", this->file_offset);
  }

  auto* callee_context = this->global->context_for_function(a->callee_function_id);
  if (a->callee_function_id == 0) {
    throw compile_error(string_printf("function %" PRId64 " has no context object", a->callee_function_id),
        this->file_offset);
  }

  // order of arguments:
  // 1. positional arguments, in the order defined in the function
  // 2. keyword arguments, in the order defined in the function
  // 3. variadic arguments
  // we currently don't support variadic keyword arguments at all

  // TODO: figure out how to support variadic arguments
  if (a->varargs.get() || a->varkwargs.get()) {
    throw compile_error("variadic function calls not supported", this->file_offset);
  }
  if (!callee_context->varargs_name.empty() || !callee_context->varkwargs_name.empty()) {
    throw compile_error("variadic function definitions not supported", this->file_offset);
  }

  this->as.write_label(string_printf("__FunctionCall_%p_push_registers", a));

  // separate out the positional and keyword args from the call args
  const vector<shared_ptr<Expression>>& positional_call_args = a->args;
  const unordered_map<string, shared_ptr<Expression>> keyword_call_args = a->kwargs;

  struct ArgumentValue {
    shared_ptr<Expression> passed_value;
    Variable default_value; // Indeterminate for positional args
  };
  vector<ArgumentValue> arg_values;
  vector<Variable> arg_types;

  // push positional args first
  size_t arg_index = 0;
  for (; arg_index < positional_call_args.size(); arg_index++) {
    if (arg_index >= callee_context->args.size()) {
      throw compile_error("too many arguments in function call", this->file_offset);
    }

    const auto& callee_arg = callee_context->args[arg_index];
    const auto& call_arg = positional_call_args[arg_index];

    // if the callee arg is a kwarg and the call also includes that kwarg, fail
    if ((callee_arg.default_value.type != ValueType::Indeterminate) &&
        keyword_call_args.count(callee_arg.name)) {
      throw compile_error(string_printf("argument %s specified multiple times",
          callee_arg.name.c_str()), this->file_offset);
    }

    arg_values.emplace_back();
    arg_values.back().passed_value = call_arg;
    arg_values.back().default_value = Variable(ValueType::Indeterminate);
  }

  // now push keyword args, in the order the function defines them
  for (; arg_index < callee_context->args.size(); arg_index++) {
    const auto& callee_arg = callee_context->args[arg_index];

    arg_values.emplace_back();
    try {
      const auto& call_arg = keyword_call_args.at(callee_arg.name);
      arg_values.back().passed_value = call_arg;
      arg_values.back().default_value = Variable(ValueType::Indeterminate);

    } catch (const out_of_range& e) {
      arg_values.back().passed_value.reset();
      arg_values.back().default_value = callee_arg.default_value;
    }
  }

  // at this point we should have the same number of args overall as the
  // function wants
  if (arg_values.size() != callee_context->args.size()) {
    throw compile_error("incorrect argument count in function call", this->file_offset);
  }

  // reserve stack space appropriately
  ssize_t arg_stack_bytes = this->write_function_call_stack_prep(arg_values.size());

  // now generate code for all the arguments
  for (arg_index = 0; arg_index < arg_values.size(); arg_index++) {
    auto& arg = arg_values[arg_index];
    if (arg.passed_value.get()) {
      this->as.write_label(string_printf("__FunctionCall_%p_evaluate_arg_%zu_passed_value",
          a, arg_index));
      this->write_code_for_call_arg(arg.passed_value, arg_index);
      arg_types.emplace_back(move(this->current_type));
    } else {
      this->as.write_label(string_printf("__FunctionCall_%p_evaluate_arg_%zu_default_value",
          a, arg_index));
      this->write_code_for_call_arg(arg.default_value, arg_index);
      arg_types.emplace_back(arg.default_value);
    }
  }

  // we can now unreserve the argument registers
  for (size_t arg_index = 0;
       (arg_index < argument_register_order.size()) &&
         (arg_index < arg_values.size());
       arg_index++) {
    this->release_register(argument_register_order[arg_index]);
  }

  // any remaining reserved registers will need to be saved
  // TODO: for now, just assert that there are no reserved registers (I'm lazy)
  if (this->available_registers != default_available_registers) {
    throw compile_error(string_printf("some registers were reserved at function call (%" PRIX64 " available, %" PRIX64 " expected)",
        this->available_registers, default_available_registers), this->file_offset);
  }

  // figure out which fragment to call. if a fragment exists that has this
  // signature, use it; otherwise, generate a call to the compiler instead
  string arg_signature = type_signature_for_variables(arg_types);
  try {
    int64_t fragment_id = callee_context->arg_signature_to_fragment_id.at(arg_signature);
    const auto& fragment = callee_context->fragments.at(fragment_id);

    this->as.write_label(string_printf("__FunctionCall_%p_call_fragment_%" PRId64 "_%" PRId64,
          a, a->callee_function_id, fragment_id));
    // TODO: deal with return value somehow
    this->write_function_call(fragment.compiled, {}, arg_stack_bytes);

  } catch (const std::out_of_range& e) {
    // the fragment doesn't exist. we won't build it just yet; we'll generate a
    // call to the compiler that will build it later
    // TODO
    throw compile_error("referenced fragment does not exist", this->file_offset);
  }

  // TODO: call destructors on arguments (not doing so is a memory leak!)
}

void CompilationVisitor::visit(ArrayIndex* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("ArrayIndex not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(ArraySlice* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("ArraySlice not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(IntegerConstant* a) {
  this->file_offset = a->file_offset;

  this->as.write_mov(this->target_register, a->value);
  this->current_type = Variable(ValueType::Int);
}

void CompilationVisitor::visit(FloatConstant* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("FloatConstant not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(BytesConstant* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("BytesConstant not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(UnicodeConstant* a) {
  this->file_offset = a->file_offset;

  int64_t available = this->write_push_reserved_registers();

  const UnicodeObject* o = this->global->get_or_create_constant(a->value);
  this->as.write_mov(Register::RDI, reinterpret_cast<int64_t>(o));
  this->write_function_call(reinterpret_cast<const void*>(add_reference), {},
      this->write_function_call_stack_prep(1), this->target_register);

  this->write_pop_reserved_registers(available);

  this->current_type = Variable(ValueType::Unicode);
}

void CompilationVisitor::visit(TrueConstant* a) {
  this->file_offset = a->file_offset;

  this->as.write_mov(this->target_register, 1);
  this->current_type = Variable(ValueType::Bool);
}

void CompilationVisitor::visit(FalseConstant* a) {
  this->file_offset = a->file_offset;

  this->as.write_mov(this->target_register, 0);
  this->current_type = Variable(ValueType::Bool);
}

void CompilationVisitor::visit(NoneConstant* a) {
  this->file_offset = a->file_offset;

  this->as.write_mov(this->target_register, 0);
  this->current_type = Variable(ValueType::None);
}

void CompilationVisitor::visit(VariableLookup* a) {
  this->file_offset = a->file_offset;

  VariableLocation loc = this->location_for_variable(a->name);

  if (loc.is_global) {
    this->as.write_mov(MemoryReference(this->target_register),
        MemoryReference(Register::R13, this->module->global_base_offset + loc.offset));
  } else {
    this->as.write_mov(MemoryReference(this->target_register),
        MemoryReference(Register::RBP, loc.offset));
  }
  this->current_type = loc.type;
}

void CompilationVisitor::visit(AttributeLookup* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("AttributeLookup not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(TupleLValueReference* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("TupleLValueReference not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(ArrayIndexLValueReference* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("ArrayIndexLValueReference not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(ArraySliceLValueReference* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("ArraySliceLValueReference not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(AttributeLValueReference* a) {
  this->file_offset = a->file_offset;

  if (a->base.get()) {
    throw compile_error("AttributeLValueReference with nontrivial base not yet implemented", this->file_offset);
  }

  this->lvalue_target = this->location_for_variable(a->name);
}

// statement visitation
void CompilationVisitor::visit(ModuleStatement* a) {
  this->file_offset = a->file_offset;

  this->as.write_label(string_printf("__ModuleStatement_%p", a));

  this->stack_bytes_used = 8;

  // this is essentially a function, but it will only be called once. we still
  // have to treat it like a function, but it has no local variables (everything
  // it writes is global, so based on R13, not RSP). the global pointer is
  // passed as an argument (RDI) instead of already being in R13, so we move it
  // into place.
  this->write_push(Register::RBP);
  this->as.write_mov(MemoryReference(Register::RBP),
      MemoryReference(Register::RSP));
  this->write_push(Register::R13);
  this->as.write_mov(MemoryReference(Register::R13),
      MemoryReference(Register::RDI));

  // generate the function's code
  this->RecursiveASTVisitor::visit(a);

  // hooray we're done
  this->as.write_label(string_printf("__ModuleStatement_%p_return", a));
  this->write_pop(Register::R13);
  this->write_pop(Register::RBP);

  if (this->stack_bytes_used != 8) {
    throw compile_error(string_printf(
        "stack misaligned at end of module root scope (%" PRId64 " bytes used; should be 8)",
        this->stack_bytes_used), this->file_offset);
  }

  this->as.write_ret();
}

void CompilationVisitor::visit(ExpressionStatement* a) {
  this->file_offset = a->file_offset;

  // just evaluate it into some random register
  this->target_register = this->available_register();
  a->expr->accept(this);
}

void CompilationVisitor::visit(AssignmentStatement* a) {
  this->file_offset = a->file_offset;

  this->as.write_label(string_printf("__AssignmentStatement_%p", a));

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
  this->as.write_label(string_printf("__AssignmentStatement_%p_write_value", a));
  if (this->lvalue_target.is_global) {
    this->as.write_mov(
        MemoryReference(Register::R13, this->module->global_base_offset + this->lvalue_target.offset),
        MemoryReference(this->target_register));
  } else {
    this->as.write_mov(
        MemoryReference(Register::RBP, this->lvalue_target.offset),
        MemoryReference(this->target_register));
  }
}

void CompilationVisitor::visit(AugmentStatement* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("AugmentStatement not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(DeleteStatement* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("DeleteStatement not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(ImportStatement* a) {
  this->file_offset = a->file_offset;

  // we actually don't have to do anything here! all imports are done
  // statically, so the names already exist in the current scope and are linked
  // to the right objects
}

void CompilationVisitor::visit(GlobalStatement* a) {
  this->file_offset = a->file_offset;
  // nothing to do here; AnnotationVisitor already extracted all useful info
}

void CompilationVisitor::visit(ExecStatement* a) {
  this->file_offset = a->file_offset;

  // we don't support this
  throw compile_error("ExecStatement is not supported", this->file_offset);
}

void CompilationVisitor::visit(AssertStatement* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("AssertStatement not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(BreakStatement* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("BreakStatement not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(ContinueStatement* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("ContinueStatement not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(ReturnStatement* a) {
  this->file_offset = a->file_offset;

  // the value should be returned in rax
  this->target_register = Register::RAX;
  a->value->accept(this);
  // TODO: generate return or jmp opcodes appropriately
  throw compile_error("ReturnStatement not completely implemented", this->file_offset);
}

void CompilationVisitor::visit(RaiseStatement* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("RaiseStatement not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(YieldStatement* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("YieldStatement not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(SingleIfStatement* a) {
  this->file_offset = a->file_offset;

  throw logic_error("SingleIfStatement used instead of subclass");
}

void CompilationVisitor::visit(IfStatement* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("IfStatement not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(ElseStatement* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("ElseStatement not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(ElifStatement* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("ElifStatement not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(ForStatement* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("ForStatement not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(WhileStatement* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("WhileStatement not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(ExceptStatement* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("ExceptStatement not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(FinallyStatement* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("FinallyStatement not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(TryStatement* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("TryStatement not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(WithStatement* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("WithStatement not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(FunctionDefinition* a) {
  this->file_offset = a->file_offset;

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
      this->as.write_mov(this->target_register, context);
      this->as.write_mov(
          MemoryReference(Register::R13, this->module->global_base_offset + target_offset),
          MemoryReference(this->target_register));

    } else {
      ssize_t target_offset = -static_cast<ssize_t>(distance(
          this->target_function->locals.begin(),
          this->target_function->locals.find(a->name))) * sizeof(int64_t);
      // TODO: use `mov [r+off], val` (not yet implemented in the assembler)
      this->as.write_mov(this->target_register, context);
      this->as.write_mov(
          MemoryReference(Register::RBP, target_offset),
          MemoryReference(this->target_register));
    }
    return;
  }

  // TODO: we should generate lead-in and lead-out code here, then recur
  throw compile_error("FunctionDefinition not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(ClassDefinition* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("ClassDefinition not yet implemented", this->file_offset);
}

void CompilationVisitor::write_code_for_call_arg(
    shared_ptr<Expression> value, size_t arg_index) {
  Register original_target_register = this->target_register;
  if (arg_index < argument_register_order.size()) {
    // construct it directly into the register, then reserve the register
    this->target_register = argument_register_order[arg_index];
    value->accept(this);
    this->reserve_register(this->target_register);
  } else {
    // construct it into any register, then generate code to put it on the stack
    // in this case, don't reserve the register
    this->target_register = this->available_register();
    value->accept(this);
    this->write_push(this->target_register);
  }
  this->target_register = original_target_register;
}

void CompilationVisitor::write_code_for_call_arg(const Variable& value,
    size_t arg_index) {
  if (!value.value_known) {
    throw compile_error("can\'t generate code for unknown value", this->file_offset);
  }

  Register original_target_register = this->target_register;
  if (arg_index < argument_register_order.size()) {
    this->target_register = argument_register_order[arg_index];
  } else {
    this->target_register = this->available_register();
  }

  switch (value.type) {
    case ValueType::Indeterminate:
      throw compile_error("can\'t generate code for Indeterminate value", this->file_offset);

    case ValueType::None:
      this->as.write_mov(this->target_register, 0);
      break;

    case ValueType::Bool:
    case ValueType::Int:
      this->as.write_mov(this->target_register, value.int_value);
      break;

    case ValueType::Float:
      throw compile_error("Float default values not yet implemented", this->file_offset);

    case ValueType::Bytes: {
      const BytesObject* o = this->global->get_or_create_constant(*value.bytes_value);
      this->as.write_mov(Register::RDI, reinterpret_cast<int64_t>(o));
      this->write_function_call(reinterpret_cast<const void*>(&add_reference),
          {}, this->write_function_call_stack_prep(1), this->target_register);
      break;
    }

    case ValueType::Unicode: {
      const UnicodeObject* o = this->global->get_or_create_constant(*value.unicode_value);
      this->as.write_mov(Register::RDI, reinterpret_cast<int64_t>(o));
      this->write_function_call(reinterpret_cast<const void*>(&add_reference),
          {}, this->write_function_call_stack_prep(1), this->target_register);
      break;
    }

    case ValueType::List:
      throw compile_error("List default values not yet implemented", this->file_offset);
    case ValueType::Tuple:
      throw compile_error("Tuple default values not yet implemented", this->file_offset);
    case ValueType::Set:
      throw compile_error("Set default values not yet implemented", this->file_offset);
    case ValueType::Dict:
      throw compile_error("Dict default values not yet implemented", this->file_offset);
    case ValueType::Function:
      throw compile_error("Function default values not yet implemented", this->file_offset);
    case ValueType::Class:
      throw compile_error("Class default values not yet implemented", this->file_offset);
    case ValueType::Module:
      throw compile_error("Module default values not yet implemented", this->file_offset);
    default:
      throw compile_error("default value has unknown type", this->file_offset);
  }

  if (arg_index < argument_register_order.size()) {
    this->reserve_register(this->target_register);
  } else {
    this->write_push(this->target_register);
  }
  this->target_register = original_target_register;
}

ssize_t CompilationVisitor::write_function_call_stack_prep(size_t arg_count) {
  ssize_t arg_stack_bytes = (arg_count > argument_register_order.size()) ?
      ((arg_count - argument_register_order.size()) * sizeof(int64_t)) : 0;
  // make sure the stack will be aligned at call time
  arg_stack_bytes += 0x10 - ((this->stack_bytes_used + arg_stack_bytes) & 0x0F);
  if (arg_stack_bytes) {
    this->adjust_stack(-arg_stack_bytes);
  }
  return arg_stack_bytes;
}

void CompilationVisitor::write_function_call(const void* function,
    const std::vector<const MemoryReference>& args, ssize_t arg_stack_bytes,
    Register return_register) {

  size_t rsp_adjustment = 0;
  if (arg_stack_bytes < 0) {
    arg_stack_bytes = this->write_function_call_stack_prep(args.size());
    // if any of the references are memory references based on RSP, we'll have
    // to adjust them
    rsp_adjustment = arg_stack_bytes;
  }

  // generate the list of move destinations
  vector<MemoryReference> dests;
  for (size_t x = 0; x < args.size(); x++) {
    if (x < argument_register_order.size()) {
      dests.emplace_back(argument_register_order[x]);
    } else {
      dests.emplace_back(Register::RSP, (x - argument_register_order.size()) * 8);
    }
  }

  // deal with conflicting moves by making a graph of the moves. in this graph,
  // there's an edge from m1 to m2 if m1.src == m2.dest. this means m1 has to be
  // done before m2 to maintain correct values. then we can just do a
  // topological sort on this graph and do the moves in that order. but watch
  // out: the graph can have cycles, and we'll have to break them somehow,
  // probably by using stack space

  unordered_map<size_t, unordered_set<size_t>> move_to_dependents;
  for (size_t x = 0; x < args.size(); x++) {
    for (size_t y = 0; y < args.size(); y++) {
      if (x == y) {
        continue;
      }
      if (args[x] == dests[y]) {
        move_to_dependents[x].insert(y);
      }
    }
  }

  // DFS-based topological sort. for now just fail if a cycle is detected.
  // TODO: deal with cycles somehow
  deque<size_t> move_order;
  vector<bool> moves_considered(args.size(), false);
  vector<bool> moves_in_progress(args.size(), false);
  std::function<void(size_t)> visit_move = [&](size_t x) {
    if (moves_in_progress[x]) {
      throw compile_error("cyclic argument move dependency", this->file_offset);
    }
    if (moves_considered[x]) {
      return;
    }
    moves_in_progress[x] = true;
    for (size_t y : move_to_dependents[x]) {
      visit_move(y);
    }
    moves_in_progress[x] = false;
    moves_considered[x] = true;
    move_order.emplace_front(x);
  };
  for (size_t x = 0; x < args.size(); x++) {
    if (moves_considered[x]) {
      continue;
    }
    visit_move(x);
  }

  // generate the mov opcodes in the determined order
  for (size_t arg_index : move_order) {
    auto& ref = args[arg_index];
    auto& dest = dests[arg_index];
    if (ref == dest) {
      continue;
    }
    if ((ref.base_register == Register::RSP) && ref.field_size) {
      MemoryReference new_ref(ref.base_register, ref.offset + rsp_adjustment,
          ref.index_register, ref.field_size);
      this->as.write_mov(dest, new_ref);
    } else {
      this->as.write_mov(dest, ref);
    }
  }

  // finally, call the function
  this->as.write_mov(Register::RAX, reinterpret_cast<int64_t>(function));
  this->as.write_call(MemoryReference(Register::RAX));

  // put the return value into the target register
  if ((return_register != Register::None) &&
      (return_register != Register::RAX)) {
    this->as.write_mov(MemoryReference(this->target_register),
        MemoryReference(Register::RAX));
  }

  // reclaim any reserved stack space
  if (arg_stack_bytes) {
    this->adjust_stack(arg_stack_bytes);
  }
}

void CompilationVisitor::write_destructor_call(const MemoryReference& mem,
    const Variable& type) {
  switch (type.type) {
    case ValueType::Indeterminate:
      throw compile_error("can\'t call destructor for Indeterminate value", this->file_offset);

    // these types have a trivial destructor - nothing needs to be done
    case ValueType::None:
    case ValueType::Bool:
    case ValueType::Int:
    case ValueType::Float:
    // these types do not have refcounts and cannot be destroyed
    case ValueType::Function:
    case ValueType::Class:
    case ValueType::Module:
      break;

    case ValueType::Bytes:
    case ValueType::Unicode:
      // these types use basic_remove_reference
      this->write_function_call(
          reinterpret_cast<const void*>(&basic_remove_reference), {mem});
      break;

    case ValueType::List:
      throw compile_error("destructor call not yet implemented", this->file_offset);
      break;

    case ValueType::Tuple:
      throw compile_error("destructor call not yet implemented", this->file_offset);
      break;

    case ValueType::Set:
      throw compile_error("destructor call not yet implemented", this->file_offset);
      break;

    case ValueType::Dict:
      throw compile_error("destructor call not yet implemented", this->file_offset);
      break;
  }
}

void CompilationVisitor::write_push(Register reg) {
  this->as.write_push(reg);
  this->stack_bytes_used += 8;
}

void CompilationVisitor::write_pop(Register reg) {
  this->stack_bytes_used -= 8;
  this->as.write_pop(reg);
}

void CompilationVisitor::adjust_stack(ssize_t bytes) {
  if (bytes < 0) {
    this->as.write_sub(MemoryReference(Register::RSP), -bytes);
  } else {
    this->as.write_add(MemoryReference(Register::RSP), bytes);
  }
  this->stack_bytes_used -= bytes;
}

CompilationVisitor::VariableLocation CompilationVisitor::location_for_variable(
    const string& name) {
  VariableLocation loc;

  // if we're writing a global, use its global slot offset (from R13)
  if (!this->target_function || this->target_function->globals.count(name)) {
    auto it = this->module->globals.find(name);
    if (it == this->module->globals.end()) {
      throw compile_error("nonexistent global: " + name, this->file_offset);
    }
    ssize_t offset = distance(this->module->globals.begin(), it);
    loc.offset = offset * sizeof(int64_t);
    loc.is_global = true;
    loc.type = it->second;

  // if we're writing a local, use its local slot offset (from RBP)
  } else {
    auto it = this->target_function->locals.find(name);
    if (it == this->target_function->locals.end()) {
      throw compile_error("nonexistent local: " + name, this->file_offset);
    }
    ssize_t offset = distance(this->target_function->locals.begin(), it);
    offset -= this->target_function->locals.size();
    loc.offset = offset * sizeof(int64_t);
    loc.is_global = false;
    loc.type = it->second;
  }

  return loc;
}
