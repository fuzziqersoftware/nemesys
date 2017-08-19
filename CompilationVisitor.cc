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
#include "Types/Reference.hh"
#include "Types/Strings.hh"

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
// R14 = callee = exception block pointer
// R15 = callee = active exception
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
    ModuleAnalysis* module, int64_t target_function_id, int64_t target_split_id,
    const unordered_map<string, Variable>* local_overrides) : file_offset(-1),
    global(global), module(module),
    target_function(this->global->context_for_function(target_function_id)),
    target_split_id(target_split_id),
    available_registers(default_available_registers),
    target_register(Register::None), stack_bytes_used(0) {

  if (target_function_id) {

    // sanity check
    if (!this->target_function) {
      throw compile_error(string_printf("function %" PRId64 " apparently does not exist", target_function_id));
    }
    if (!local_overrides) {
      throw compile_error("no local overrides given to function call");
    }
    this->local_overrides = *local_overrides;

    // make sure everything in local_overrides actually refers to a local
    for (const auto& it : this->local_overrides) {
      if (!this->target_function->locals.count(it.first)) {
        throw compile_error(string_printf("argument %s in function %" PRId64 " overrides a nonlocal variable", it.first.c_str(), target_function_id));
      }
    }
  }
}

AMD64Assembler& CompilationVisitor::assembler() {
  return this->as;
}

const unordered_set<Variable>& CompilationVisitor::return_types() {
  return this->function_return_types;
}



Register CompilationVisitor::reserve_register(Register which) {
  if (which == Register::None) {
    which = this->available_register();
  }
  if (!(this->available_registers & (1 << which))) {
    throw compile_error(string_printf("register %s is not available", name_for_register(which)), this->file_offset);
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

      this->write_delete_reference(this->target_register, this->current_type);
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

  switch (type.type) {
    case ValueType::Indeterminate:
      throw compile_error("truth value test on Indeterminate type", this->file_offset);

    case ValueType::Bool:
    case ValueType::Int:
      this->as.write_test(MemoryReference(reg), MemoryReference(reg));
      break;

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
      this->as.write_mov(MemoryReference(size_reg), MemoryReference(reg, 0x10));
      this->as.write_test(MemoryReference(size_reg), MemoryReference(size_reg));
      break;
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
  // TODO: it's kind of stupid that we push the result onto the stack; figure
  // out a way to implement this without using memory access
  this->as.write_label(string_printf("__BinaryOperation_%p_evaluate_left", a));
  a->left->accept(this);
  this->write_push(this->target_register); // so right doesn't clobber it
  Variable left_type = move(this->current_type);

  this->as.write_label(string_printf("__BinaryOperation_%p_evaluate_right", a));
  a->right->accept(this);
  this->write_push(this->target_register); // for the destructor call later
  Variable& right_type = this->current_type;

  MemoryReference left_mem(Register::RSP, 8);
  MemoryReference right_mem(Register::RSP, 0);
  MemoryReference target_mem(this->target_register);

  // pick a temporary register that isn't the target register
  this->reserve_register(this->target_register);
  MemoryReference temp_mem(this->available_register());
  this->release_register(this->target_register);

  this->as.write_label(string_printf("__BinaryOperation_%p_combine", a));
  switch (a->oper) {
    case BinaryOperator::LessThan:
    case BinaryOperator::GreaterThan:
    case BinaryOperator::LessOrEqual:
    case BinaryOperator::GreaterOrEqual:
    case BinaryOperator::Equality:
    case BinaryOperator::NotEqual:
      // integer comparisons
      if (((left_type.type == ValueType::Int) ||
           (left_type.type == ValueType::Bool)) &&
          ((right_type.type == ValueType::Int) ||
           (right_type.type == ValueType::Bool))) {
        this->as.write_mov(target_mem, 0);
        this->as.write_mov(temp_mem, left_mem);
        this->as.write_cmp(temp_mem, right_mem);
        target_mem.base_register = byte_register_for_register(target_mem.base_register);
        if (a->oper == BinaryOperator::LessThan) {
          this->as.write_setl(target_mem);
        } else if (a->oper == BinaryOperator::GreaterThan) {
          this->as.write_setg(target_mem);
        } else if (a->oper == BinaryOperator::LessOrEqual) {
          this->as.write_setle(target_mem);
        } else if (a->oper == BinaryOperator::GreaterOrEqual) {
          this->as.write_setge(target_mem);
        } else if (a->oper == BinaryOperator::Equality) {
          this->as.write_sete(target_mem);
        } else if (a->oper == BinaryOperator::NotEqual) {
          this->as.write_setne(target_mem);
        }
      } else {
        throw compile_error("operator not yet implemented", this->file_offset);
      }
      this->current_type = Variable(ValueType::Bool);
      break;

    case BinaryOperator::In:
    case BinaryOperator::NotIn:
      if (((left_type.type == ValueType::Bytes) &&
           (right_type.type == ValueType::Bytes)) ||
          ((left_type.type == ValueType::Unicode) &&
           (right_type.type == ValueType::Unicode))) {
        const void* target_function = (left_type.type == ValueType::Bytes) ?
            reinterpret_cast<const void*>(&bytes_contains) :
            reinterpret_cast<const void*>(&unicode_contains);
        this->write_function_call(target_function,
            NULL, {MemoryReference(this->target_register),
              MemoryReference(Register::RSP, 8)}, -1,
            this->target_register);
      } else {
        // TODO
        throw compile_error("In/NotIn not yet implemented", this->file_offset);
      }
      if (a->oper == BinaryOperator::NotIn) {
        this->as.write_xor(this->target_register, 1);
      }
      this->current_type = Variable(ValueType::Bool);
      break;

    case BinaryOperator::Is:
      // TODO
      this->current_type = Variable(ValueType::Bool);
      throw compile_error("Is not yet implemented", this->file_offset);
    case BinaryOperator::IsNot:
      // TODO
      this->current_type = Variable(ValueType::Bool);
      throw compile_error("IsNot not yet implemented", this->file_offset);

    case BinaryOperator::Or:
      if (((left_type.type == ValueType::Int) ||
           (left_type.type == ValueType::Bool)) &&
          ((right_type.type == ValueType::Int) ||
           (right_type.type == ValueType::Bool))) {
        this->as.write_or(MemoryReference(this->target_register),
            MemoryReference(Register::RSP, 8));
        break;
      }
      throw compile_error("Or only valid for integer types", this->file_offset);

    case BinaryOperator::And:
      if (((left_type.type == ValueType::Int) ||
           (left_type.type == ValueType::Bool)) &&
          ((right_type.type == ValueType::Int) ||
           (right_type.type == ValueType::Bool))) {
        this->as.write_and(MemoryReference(this->target_register),
            MemoryReference(Register::RSP, 8));
        break;
      }
      throw compile_error("And only valid for integer types", this->file_offset);

    case BinaryOperator::Xor:
      if (((left_type.type == ValueType::Int) ||
           (left_type.type == ValueType::Bool)) &&
          ((right_type.type == ValueType::Int) ||
           (right_type.type == ValueType::Bool))) {
        this->as.write_xor(MemoryReference(this->target_register),
            MemoryReference(Register::RSP, 8));
        break;
      }
      throw compile_error("Xor only valid for integer types", this->file_offset);

    case BinaryOperator::LeftShift:
    case BinaryOperator::RightShift:
      if (((left_type.type == ValueType::Int) ||
           (left_type.type == ValueType::Bool)) &&
          ((right_type.type == ValueType::Int) ||
           (right_type.type == ValueType::Bool))) {
        // we can only use cl apparently
        if (this->reserve_register(Register::RCX) != Register::RCX) {
          throw compile_error("RCX not available for shift operation");
        }
        this->release_register(Register::RCX);
        this->as.write_mov(MemoryReference(Register::RCX),
            MemoryReference(this->target_register));
        this->as.write_mov(MemoryReference(this->target_register),
            MemoryReference(Register::RSP, 8));
        if (a->oper == BinaryOperator::LeftShift) {
          this->as.write_shl_cl(MemoryReference(this->target_register));
        } else {
          this->as.write_sar_cl(MemoryReference(this->target_register));
        }
        break;
      }
      throw compile_error("LeftShift/RightShift only valid for integer types", this->file_offset);

    case BinaryOperator::Addition:
      if ((left_type.type == ValueType::Bytes) &&
          (right_type.type == ValueType::Bytes)) {
        this->write_function_call(reinterpret_cast<const void*>(&bytes_concat),
            NULL, {MemoryReference(Register::RSP, 8),
              MemoryReference(this->target_register)}, -1,
            this->target_register);
        break;
      } else if ((left_type.type == ValueType::Unicode) &&
                 (right_type.type == ValueType::Unicode)) {
        this->write_function_call(reinterpret_cast<const void*>(&unicode_concat),
            NULL, {MemoryReference(Register::RSP, 8),
              MemoryReference(this->target_register)}, -1,
            this->target_register);
        break;
      } else if (((left_type.type == ValueType::Int) ||
                  (left_type.type == ValueType::Bool)) &&
                 ((right_type.type == ValueType::Int) ||
                  (right_type.type == ValueType::Bool))) {
        this->as.write_add(MemoryReference(this->target_register),
            MemoryReference(Register::RSP, 8));
        break;
      }

      // TODO
      throw compile_error("Addition not yet implemented for these types", this->file_offset);

    case BinaryOperator::Subtraction:
      if (((left_type.type == ValueType::Int) ||
           (left_type.type == ValueType::Bool)) &&
          ((right_type.type == ValueType::Int) ||
           (right_type.type == ValueType::Bool))) {
        this->as.write_neg(MemoryReference(this->target_register));
        this->as.write_add(MemoryReference(this->target_register),
            MemoryReference(Register::RSP, 8));
        break;
      }
      throw compile_error("Subtraction not valid for these types", this->file_offset);

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
      if (((left_type.type == ValueType::Int) ||
           (left_type.type == ValueType::Bool)) &&
          ((right_type.type == ValueType::Int) ||
           (right_type.type == ValueType::Bool))) {
        // TODO: deal with negative exponents (currently we just do nothing)

        // implementation mirrors notes/pow.s except that we load the base value
        // into a temp register
        string again_label = string_printf("__BinaryOperation_%p_pow_again", a);
        string skip_base_label = string_printf("__BinaryOperation_%p_pow_skip_base", a);
        as.write_mov(target_mem, 1);
        as.write_mov(temp_mem, left_mem);
        as.write_label(again_label);
        as.write_test(right_mem, 1);
        as.write_jz(skip_base_label);
        as.write_imul(target_mem.base_register, temp_mem);
        as.write_label(skip_base_label);
        as.write_imul(temp_mem.base_register, temp_mem);
        as.write_shr(right_mem, 1);
        as.write_jnz(again_label);
        break;
      }

      // TODO
      throw compile_error("Exponentiation not yet implemented", this->file_offset);

    default:
      throw compile_error("unhandled binary operator", this->file_offset);
  }

  this->as.write_label(string_printf("__BinaryOperation_%p_cleanup", a));

  // if either value requires destruction, do so now
  if (type_has_refcount(left_type.type) || type_has_refcount(right_type.type)) {
    // save the return value before destroying the temp values
    this->write_push(this->target_register);

    // destroy the temp values
    this->as.write_label(string_printf("__BinaryOperation_%p_destroy_left", a));
    this->write_delete_reference(MemoryReference(Register::RSP, 8), left_type);
    this->as.write_label(string_printf("__BinaryOperation_%p_destroy_right", a));
    this->write_delete_reference(MemoryReference(Register::RSP, 16), right_type);

    // now load the result again and clean up the stack
    this->as.write_mov(MemoryReference(this->target_register),
        MemoryReference(Register::RSP, 0));
    this->adjust_stack(0x18);

  // no destructor call necessary; just remove left and right from the stack
  } else {
    this->adjust_stack(0x10);
  }

  this->as.write_label(string_printf("__BinaryOperation_%p_complete", a));
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

  // if this definition is not the function being compiled, don't recur; instead
  // treat it as an assignment (of the function context to the local/global var)
  if (!this->target_function ||
      (this->target_function->id != a->function_id)) {
    // if the function being declared is a closure, fail
    // TODO: actually implement this check

    // write the function's context to the variable. note that Function-typed
    // variables don't point directly to the code; this is necessary for us to
    // figure out the right fragment at call time
    auto* declared_function_context = this->global->context_for_function(a->function_id);
    this->as.write_mov(this->target_register, reinterpret_cast<int64_t>(declared_function_context));
    this->current_type = Variable(a->function_id, false);
    return;
  }

  string base_label = string_printf("LambdaDefinition_%p", a);
  this->write_function_setup(base_label);

  this->target_register = Register::RAX;
  this->RecursiveASTVisitor::visit(a);
  this->function_return_types.emplace(this->current_type);

  this->write_function_cleanup(base_label);
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
    string name;
    shared_ptr<Expression> passed_value;
    Variable default_value; // Indeterminate for positional args
    Variable type;
    ssize_t stack_offset; // if < 0, this argument isn't stored on the stack
  };
  vector<ArgumentValue> arg_values;

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
    arg_values.back().name = callee_arg.name;
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

  // now start writing code

  // push all reserved registers
  int64_t previously_reserved_registers = this->write_push_reserved_registers();

  // reserve stack space as needed
  ssize_t arg_stack_bytes = write_function_call_stack_prep(arg_values.size());

  // now generate code for the arguments
  Register original_target_register = this->target_register;
  for (arg_index = 0; arg_index < arg_values.size(); arg_index++) {
    auto& arg = arg_values[arg_index];

    // figure out where to construct it into
    if (arg_index < argument_register_order.size()) {
      this->target_register = argument_register_order[arg_index];
      arg.stack_offset = -1; // passed through register only
    } else {
      this->target_register = this->available_register();
      arg.stack_offset = sizeof(int64_t) * (arg_index - argument_register_order.size());
    }
    if (arg.stack_offset >= arg_stack_bytes) {
      throw compile_error(string_printf(
          "argument stack overflow at function call time (0x%zX vs 0x%zX)",
          arg.stack_offset, arg_stack_bytes), this->file_offset);
    }

    // generate code for the argument's value
    if (arg.passed_value.get()) {
      this->as.write_label(string_printf("__FunctionCall_%p_evaluate_arg_%zu_passed_value",
          a, arg_index));
      arg.passed_value->accept(this);
      arg.type = move(this->current_type);
    } else {
      this->as.write_label(string_printf("__FunctionCall_%p_evaluate_arg_%zu_default_value",
          a, arg_index));
      this->write_code_for_value(arg.default_value);
      arg.type = arg.default_value;
    }

    // store the value on the stack if needed; otherwise, mark the regsiter as
    // reserved
    if (arg.stack_offset < 0) {
      this->reserve_register(this->target_register);
    } else {
      this->as.write_mov(MemoryReference(Register::RSP, arg.stack_offset),
          MemoryReference(this->target_register));
    }
  }
  this->target_register = original_target_register;

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

  // figure out which fragment to call
  vector<Variable> arg_types;
  unordered_map<string, Variable> callee_local_overrides;
  for (const auto& arg : arg_values) {
    arg_types.emplace_back(arg.type);
    callee_local_overrides.emplace(arg.name, arg.type);
  }

  // get the argument type signature
  string arg_signature;
  try {
    arg_signature = type_signature_for_variables(arg_types);
  } catch (const invalid_argument&) {
    throw compile_error("can\'t generate argument signature for function call", this->file_offset);
  }

  // generate a fragment id if necessary
  int64_t fragment_id;
  try {
    fragment_id = callee_context->arg_signature_to_fragment_id.at(arg_signature);
  } catch (const std::out_of_range& e) {
    // built-in functions can't be recompiled
    if (!callee_context->module) {
      throw compile_error(string_printf("built-in fragment %" PRId64 "+%s does not exist",
          a->callee_function_id, arg_signature.c_str()), this->file_offset);
    }

    fragment_id = callee_context->arg_signature_to_fragment_id.emplace(
        arg_signature, callee_context->fragments.size()).first->second;
  }

  // generate the fragment if necessary
  const FunctionContext::Fragment* fragment;
  try {
    fragment = &callee_context->fragments.at(fragment_id);
  } catch (const std::out_of_range& e) {
    // o noez - it doesn't exist! try to compile it
    auto new_fragment = this->global->compile_scope(callee_context->module,
        callee_context, &callee_local_overrides);
    fragment = &callee_context->fragments.emplace(fragment_id, move(new_fragment)).first->second;
  }

  this->as.write_label(string_printf("__FunctionCall_%p_call_fragment_%" PRId64 "_%" PRId64 "_%s",
      a, a->callee_function_id, fragment_id, arg_signature.c_str()));

  // call the fragment
  this->as.write_mov(Register::RAX, reinterpret_cast<int64_t>(fragment->compiled));
  this->as.write_call(MemoryReference(Register::RAX));

  // put the return value into the target register
  if (this->target_register != Register::RAX) {
    this->as.write_label(string_printf("__FunctionCall_%p_save_return_value", a));
    this->as.write_mov(MemoryReference(this->target_register),
        MemoryReference(Register::RAX));
  }

  this->current_type = fragment->return_type;

  // note: we don't have to destroy the function arguments; we passed the
  // references that we generated directly into the function and it's
  // responsible for deleting those references

  // unreserve the argument stack space
  if (arg_stack_bytes) {
    this->as.write_label(string_printf("__FunctionCall_%p_restore_stack", a));
    this->adjust_stack(arg_stack_bytes);
  }

  // pop the registers that we saved earlier
  this->write_pop_reserved_registers(previously_reserved_registers);
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

  const BytesObject* o = this->global->get_or_create_constant(a->value);
  this->as.write_mov(this->target_register, reinterpret_cast<int64_t>(o));
  this->write_add_reference(MemoryReference(this->target_register, 0));

  this->current_type = Variable(ValueType::Bytes);
}

void CompilationVisitor::visit(UnicodeConstant* a) {
  this->file_offset = a->file_offset;

  const UnicodeObject* o = this->global->get_or_create_constant(a->value);
  this->as.write_mov(this->target_register, reinterpret_cast<int64_t>(o));
  this->write_add_reference(MemoryReference(this->target_register, 0));

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

  // if this is an object, add a reference to it; otherwise just load it
  this->as.write_mov(MemoryReference(this->target_register), loc.mem);
  if (type_has_refcount(loc.type.type)) {
    this->write_add_reference(MemoryReference(this->target_register, 0));
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
  // into place. it returns the active exception object (NULL means success).
  this->write_push(Register::RBP);
  this->as.write_mov(MemoryReference(Register::RBP),
      MemoryReference(Register::RSP));
  this->write_push(Register::R12);
  this->as.write_mov(MemoryReference(Register::R12), 0);
  this->write_push(Register::R13);
  this->as.write_mov(MemoryReference(Register::R13),
      MemoryReference(Register::RDI));
  this->write_push(Register::R14);
  this->as.write_mov(MemoryReference(Register::R14), 0);
  this->write_push(Register::R15);
  this->as.write_mov(MemoryReference(Register::R15), 0);

  // generate the function's code
  this->RecursiveASTVisitor::visit(a);

  // hooray we're done
  this->as.write_label(string_printf("__ModuleStatement_%p_return", a));
  this->as.write_mov(MemoryReference(Register::RAX),
      MemoryReference(Register::R15));
  this->write_pop(Register::R15);
  this->write_pop(Register::R14);
  this->write_pop(Register::R13);
  this->write_pop(Register::R12);
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

  // typecheck the result. the type of a variable can only be changed if it's
  // Indeterminate; otherwise it's an error
  // TODO: deduplicate some of this with location_for_variable
  Variable* target_variable = NULL;
  if (this->lvalue_target.is_global) {
    target_variable = &this->module->globals.at(this->lvalue_target.name);
  } else {
    try {
      target_variable = &this->local_overrides.at(this->lvalue_target.name);
    } catch (const out_of_range&) {
      target_variable = &this->target_function->locals.at(this->lvalue_target.name);
    }
  }
  if (!target_variable) {
    throw compile_error("target variable not found");
  }
  if (target_variable->type == ValueType::Indeterminate) {
    *target_variable = this->current_type;
  } else if (*target_variable != this->lvalue_target.type) {
    string target_type_str = target_variable->str();
    string value_type_str = this->current_type.str();
    throw compile_error(string_printf("variable %s changes type from %s to %s\n",
        this->lvalue_target.name.c_str(), target_type_str.c_str(),
        value_type_str.c_str()), this->file_offset);
  }

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

  if (!this->target_function) {
    throw compile_error("return statement outside function definition", a->file_offset);
  }

  // the value should be returned in rax
  this->target_register = Register::RAX;
  a->value->accept(this);

  // record this return type
  this->function_return_types.emplace(this->current_type);

  // generate a jump to the end of the function (this is right before the
  // relevant destructor calls)
  this->as.write_jmp(this->return_label);
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

  string false_label = string_printf("__IfStatement_%p_condition_false", a);
  string end_label = string_printf("__IfStatement_%p_end", a);

  // generate the condition check
  this->as.write_label(string_printf("__IfStatement_%p_condition", a));
  this->target_register = this->available_register();
  a->check->accept(this);
  this->as.write_label(string_printf("__IfStatement_%p_test", a));
  this->write_truth_value_test(this->target_register, this->current_type);
  this->as.write_jz(false_label);

  // generate the body statements
  this->visit_list(a->items);

  // TODO: support elifs
  if (!a->elifs.empty()) {
    throw compile_error("elif clauses not yet supported");
  }

  // generate the else block, if any
  if (a->else_suite.get()) {
    this->as.write_jmp(end_label);
    this->as.write_label(false_label);
    a->else_suite->accept(this);
  }

  // any break statement will jump over the loop body and the else statement
  this->as.write_label(end_label);
}

void CompilationVisitor::visit(ElseStatement* a) {
  this->file_offset = a->file_offset;

  // uhhhh we don't have to do anything here except visit each statement, right?
  this->visit_list(a->items);
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

  string start_label = string_printf("__WhileStatement_%p_condition", a);
  string end_label = string_printf("__WhileStatement_%p_condition_false", a);
  string break_label = string_printf("__WhileStatement_%p_broken", a);

  // generate the condition check
  this->as.write_label(start_label);
  this->target_register = this->available_register();
  a->condition->accept(this);
  this->write_truth_value_test(this->target_register, this->current_type);
  this->as.write_jz(end_label);

  // generate the loop body
  this->visit_list(a->items);
  this->as.write_jmp(start_label);
  this->as.write_label(end_label);

  // if there's an else statement, generate the body here
  if (a->else_suite.get()) {
    a->else_suite->accept(this);
  }

  // any break statement will jump over the loop body and the else statement
  this->as.write_label(break_label);
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

    // if the function being declared is a closure, fail
    // TODO: actually implement this check

    // write the function's context to the variable. note that Function-typed
    // variables don't point directly to the code; this is necessary for us to
    // figure out the right fragment at call time
    auto* declared_function_context = this->global->context_for_function(a->function_id);
    auto loc = this->location_for_variable(a->name);
    this->as.write_mov(this->target_register, reinterpret_cast<int64_t>(declared_function_context));
    this->as.write_mov(loc.mem, MemoryReference(this->target_register));
    return;
  }

  string base_label = string_printf("FunctionDefinition_%p_%s", a, a->name.c_str());
  this->write_function_setup(base_label);
  this->RecursiveASTVisitor::visit(a);
  this->write_function_cleanup(base_label);
}

void CompilationVisitor::visit(ClassDefinition* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("ClassDefinition not yet implemented", this->file_offset);
}

void CompilationVisitor::write_code_for_value(const Variable& value) {
  if (!value.value_known) {
    throw compile_error("can\'t generate code for unknown value", this->file_offset);
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

    case ValueType::Bytes:
    case ValueType::Unicode: {
      const void* o = (value.type == ValueType::Bytes) ?
          reinterpret_cast<const void*>(this->global->get_or_create_constant(*value.bytes_value)) :
          reinterpret_cast<const void*>(this->global->get_or_create_constant(*value.unicode_value));
      this->as.write_mov(this->target_register, reinterpret_cast<int64_t>(o));
      this->write_add_reference(MemoryReference(this->target_register, 0));
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
}

ssize_t CompilationVisitor::write_function_call_stack_prep(size_t arg_count) {
  ssize_t arg_stack_bytes = (arg_count > argument_register_order.size()) ?
      ((arg_count - argument_register_order.size()) * sizeof(int64_t)) : 0;

  // make sure the stack will be aligned at call time
  arg_stack_bytes += ((this->stack_bytes_used + arg_stack_bytes) & 0x0F);
  if (arg_stack_bytes) {
    this->adjust_stack(-arg_stack_bytes);
  }

  return arg_stack_bytes;
}

void CompilationVisitor::write_function_call(const void* function,
    const MemoryReference* function_loc,
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
  if (function) {
    this->as.write_mov(Register::RAX, reinterpret_cast<int64_t>(function));
    this->as.write_call(MemoryReference(Register::RAX));
  } else if (function_loc) {
    this->as.write_call(*function_loc);
  } else {
    throw compile_error("can\'t generate call to no function", this->file_offset);
  }

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

void CompilationVisitor::write_function_setup(const string& base_label) {
  // get ready to rumble
  this->as.write_label("__" + base_label);
  this->stack_bytes_used = 8;

  // lead-in (stack frame setup)
  this->write_push(Register::RBP);
  this->as.write_mov(MemoryReference(Register::RBP),
      MemoryReference(Register::RSP));

  // reserve space for locals and write args into the right places
  unordered_map<string, Register> arg_to_register;
  unordered_map<string, int64_t> arg_to_stack_offset;
  for (size_t arg_index = 0; arg_index < this->target_function->args.size(); arg_index++) {
    const auto& arg = this->target_function->args[arg_index];
    if (arg_index < argument_register_order.size()) {
      arg_to_register.emplace(arg.name, argument_register_order[arg_index]);
    } else {
      // add 2, since at this point we've called the function and pushed RBP
      arg_to_stack_offset.emplace(arg.name,
          sizeof(int64_t) * (arg_index - argument_register_order.size() + 2));
    }
  }

  // set up the local space
  for (const auto& local : this->target_function->locals) {
    // if it's a register arg, push it drectly
    try {
      this->write_push(arg_to_register.at(local.first));
      continue;
    } catch (const out_of_range&) { }

    // if it's a stack arg, push it via r/m push
    try {
      this->write_push(MemoryReference(Register::RBP, arg_to_stack_offset.at(local.first)));
      continue;
    } catch (const out_of_range&) { }

    // else, initialize it to zero
    this->write_push(0);
  }

  this->return_label = string_printf("__%s_destroy_locals", base_label.c_str());
}

void CompilationVisitor::write_function_cleanup(const string& base_label) {
  // call destructors for all the local variables that have refcounts
  this->as.write_label(this->return_label);
  this->return_label.clear();
  for (auto it = this->target_function->locals.crbegin();
       it != this->target_function->locals.crend(); it++) {
    if (type_has_refcount(it->second.type)) {
      this->write_pop(Register::RDI);
      this->write_delete_reference(MemoryReference(Register::RDI), it->second);
    } else {
      // no destructor; just skip it
      // TODO: we can coalesce these for great justice
      this->adjust_stack(8);
    }
  }

  // hooray we're done
  this->as.write_label(string_printf("__%s_return", base_label.c_str()));
  this->write_pop(Register::RBP);

  if (this->stack_bytes_used != 8) {
    throw compile_error(string_printf(
        "stack misaligned at end of function (%" PRId64 " bytes used; should be 8)",
        this->stack_bytes_used), this->file_offset);
  }

  this->as.write_ret();
}

void CompilationVisitor::write_add_reference(const MemoryReference& mem) {
  this->as.write_lock();
  this->as.write_inc(mem);
}

void CompilationVisitor::write_delete_reference(const MemoryReference& mem,
    const Variable& type) {
  if (type.type == ValueType::Indeterminate) {
    throw compile_error("can\'t call destructor for Indeterminate value", this->file_offset);
  }

  if (type_has_refcount(type.type)) {
    static uint64_t skip_label_id = 0;
    Register r = this->available_register();
    string skip_label = string_printf("__delete_reference_skip_%" PRIu64,
        skip_label_id++);

    if (mem.field_size || (r != mem.base_register)) {
      this->as.write_mov(MemoryReference(r), mem);
    }
    this->as.write_lock();
    this->as.write_dec(MemoryReference(r, 0));
    this->as.write_jnz(skip_label);

    MemoryReference function_loc(r, 8);
    this->write_function_call(NULL, &function_loc, {MemoryReference(r)});
    this->as.write_label(skip_label);
  }
}

void CompilationVisitor::write_push(Register reg) {
  this->as.write_push(reg);
  this->stack_bytes_used += 8;
}

void CompilationVisitor::write_push(const MemoryReference& mem) {
  this->as.write_push(mem);
  this->stack_bytes_used += 8;
}

void CompilationVisitor::write_push(int64_t value) {
  this->as.write_push(value);
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
  loc.name = name;

  // if we're writing a global, use its global slot offset (from R13)
  if (!this->target_function || !this->target_function->locals.count(name)) {
    auto it = this->module->globals.find(name);
    if (it == this->module->globals.end()) {
      throw compile_error("nonexistent global: " + name, this->file_offset);
    }
    ssize_t offset = distance(this->module->globals.begin(), it);
    loc.offset = offset * sizeof(int64_t);
    loc.is_global = true;
    loc.type = it->second;
    loc.mem = MemoryReference(Register::R13,
        loc.offset + this->module->global_base_offset);

  // if we're writing a local, use its local slot offset (from RBP)
  } else {
    auto it = this->target_function->locals.find(name);
    if (it == this->target_function->locals.end()) {
      throw compile_error("nonexistent local: " + name, this->file_offset);
    }
    loc.offset = sizeof(int64_t) * (-static_cast<ssize_t>(1 + distance(this->target_function->locals.begin(), it)));
    loc.is_global = false;
    loc.mem = MemoryReference(Register::RBP, loc.offset);

    // use the argument type if given
    try {
      loc.type = this->local_overrides.at(name);
    } catch (const out_of_range&) {
      loc.type = it->second;
    }
  }

  return loc;
}
