#include "CompilationVisitor.hh"

#include <inttypes.h>
#include <stdlib.h>
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
#include "Types/List.hh"

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
    case ValueType::Instance:
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

  this->as.write_label(string_printf("__ListConstructor_%p_setup", a));

  // we'll use rbx to store the list ptr while constructing items and I'm lazy
  if (this->target_register == Register::RBX) {
    throw compile_error("cannot use rbx as target register for list construction");
  }
  this->write_push(Register::RBX);
  int64_t previously_reserved_registers = this->write_push_reserved_registers();

  // allocate the list object
  this->as.write_label(string_printf("__ListConstructor_%p_allocate", a));
  vector<const MemoryReference> args({
    MemoryReference(argument_register_order[0]),
    MemoryReference(argument_register_order[1]),
    MemoryReference(argument_register_order[2]),
  });
  this->as.write_mov(args[0], 0);
  this->as.write_mov(args[1], a->items.size());
  this->as.write_mov(args[2], 0);
  this->write_function_call(reinterpret_cast<void*>(list_new), NULL, args, -1,
      this->target_register);

  // save the list items pointer
  this->as.write_mov(MemoryReference(Register::RBX),
      MemoryReference(this->target_register, 0x20));
  this->write_push(Register::RAX);

  // generate code for each item and track the extension type
  Variable extension_type;
  size_t item_index = 0;
  for (const auto& item : a->items) {
    this->as.write_label(string_printf("__ListConstructor_%p_item_%zu", a, item_index));
    item->accept(this);
    this->as.write_mov(MemoryReference(Register::RBX, item_index * 8),
        MemoryReference(this->target_register));
    item_index++;

    // merge the type with the known extension type
    if (extension_type.type == ValueType::Indeterminate) {
      extension_type = move(this->current_type);
    } else if (extension_type != this->current_type) {
      throw compile_error("list contains different object types");
    }
  }

  // get the list pointer back
  this->as.write_label(string_printf("__ListConstructor_%p_finalize", a));
  this->write_pop(this->target_register);

  // if the extension type is an object of some sort, set the destructor flag
  if (type_has_refcount(extension_type.type)) {
    this->as.write_mov(MemoryReference(this->target_register, 0x18), 1);
  }

  // restore the regs we saved
  this->write_pop_reserved_registers(previously_reserved_registers);
  this->write_pop(Register::RBX);

  // the result type is List[extension_type]
  vector<Variable> extension_types({extension_type});
  this->current_type = Variable(ValueType::List, extension_types);
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
    this->current_type = Variable(ValueType::Function, a->function_id);
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

  // get the function context
  auto* fn = this->global->context_for_function(a->callee_function_id);
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
  if (!fn->varargs_name.empty() || !fn->varkwargs_name.empty()) {
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

  // for class member functions, the first argument is automatically populated
  // and is the instance object
  if (fn->class_id) {
    // if the function being called is __init__, allocate a class object first and
    // push it as the first argument
    if (fn->is_class_init()) {
      arg_values.emplace_back();
      auto& arg = arg_values.back();
      arg.name = fn->args[0].name;
      arg.passed_value = NULL;
      arg.default_value = Variable(ValueType::Instance, fn->id, nullptr);

    // if it's not __init__, we'll have to know what the instance is
    } else {
      this->as.write_label(string_printf("__FunctionCall_%p_get_instance_pointer", a));
      throw compile_error("getting the instance pointer for a method call is unimplemented");
    }
  }

  // push positional args first
  size_t call_arg_index = 0;
  size_t callee_arg_index = fn->is_class_init();
  for (; call_arg_index < positional_call_args.size();
       call_arg_index++, callee_arg_index++) {
    if (callee_arg_index >= fn->args.size()) {
      throw compile_error("too many arguments in function call", this->file_offset);
    }

    const auto& callee_arg = fn->args[callee_arg_index];
    const auto& call_arg = positional_call_args[call_arg_index];

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
  for (; callee_arg_index < fn->args.size(); callee_arg_index++) {
    const auto& callee_arg = fn->args[callee_arg_index];

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
  if (arg_values.size() != fn->args.size()) {
    throw compile_error("incorrect argument count in function call", this->file_offset);
  }

  // now start writing code

  // push all reserved registers
  int64_t previously_reserved_registers = this->write_push_reserved_registers();

  // reserve stack space as needed
  ssize_t arg_stack_bytes = write_function_call_stack_prep(arg_values.size());

  // now generate code for the arguments
  Register original_target_register = this->target_register;
  for (size_t arg_index = 0; arg_index < arg_values.size(); arg_index++) {
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

    } else if (fn->is_class_init() && (arg_index == 0)) {
      if (arg.default_value.type != ValueType::Instance) {
        throw compile_error("first argument to class constructor is not an instance", this->file_offset);
      }

      auto* cls = this->global->context_for_class(fn->id);
      if (!cls) {
        throw compile_error("__init__ call does not have an associated class");
      }

      // call malloc to create the class object
      // note: this is a semi-ugly hack, but we ignore reserved registers here
      // because this can only be the first argument - no registers can be
      // reserved at this point
      this->as.write_mov(MemoryReference(Register::RDI),
          sizeof(int64_t) * (cls->attributes.size() + 2));
      this->as.write_mov(Register::RAX, reinterpret_cast<int64_t>(&malloc));
      this->as.write_call(MemoryReference(Register::RAX));

      // fill in the refcount and destructor function
      this->as.write_mov(MemoryReference(Register::RAX, 0), 1);
      this->as.write_mov(MemoryReference(Register::RAX, 8),
          reinterpret_cast<int64_t>(cls->destructor));

      // note: we don't zero the class contents. this is ok because all of the
      // attributes must be written in __init__ (since that's the only place
      // they can be created).
      this->as.write_mov(this->target_register, MemoryReference(Register::RAX));

      arg.type = arg.default_value;

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

  // get the fragment id
  int64_t fragment_id;
  try {
    fragment_id = fn->arg_signature_to_fragment_id.at(arg_signature);

  // if there's no existing fragment with the right types, check if there's a
  // fragment with Indeterminate extension types.
  } catch (const std::out_of_range& e) {
    // TODO: for now, just clear all the extension types and see if there's a
    // match. this is an ugly hack that works for e.g. len() but won't work for
    // more complex generic functions
    for (auto& arg : arg_types) {
      for (auto& ext_type : arg.extension_types) {
        ext_type = Variable();
      }
    }
    try {
      arg_signature = type_signature_for_variables(arg_types, true);
    } catch (const invalid_argument&) {
      throw compile_error("can\'t generate argument signature for function call", this->file_offset);
    }
    try {
      fragment_id = fn->arg_signature_to_fragment_id.at(arg_signature);
    } catch (const std::out_of_range& e) {

      // still no match. built-in functions can't be recompiled, so fail
      if (!fn->module) {
        throw compile_error(string_printf("built-in fragment %" PRId64 "+%s does not exist",
            a->callee_function_id, arg_signature.c_str()), this->file_offset);
      }

      // this isn't a built-in function, so create a new fragment and compile it
      fragment_id = fn->arg_signature_to_fragment_id.emplace(
          arg_signature, fn->fragments.size()).first->second;
    }
  }

  // get or generate the Fragment object
  const FunctionContext::Fragment* fragment;
  try {
    fragment = &fn->fragments.at(fragment_id);
  } catch (const std::out_of_range& e) {
    auto new_fragment = this->global->compile_scope(fn->module, fn,
        &callee_local_overrides);
    fragment = &fn->fragments.emplace(fragment_id, move(new_fragment)).first->second;
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
  if (this->current_type.type == ValueType::Indeterminate) {
    throw compile_error("variable has Indeterminate type", this->file_offset);
  }
}

void CompilationVisitor::visit(AttributeLookup* a) {
  this->file_offset = a->file_offset;

  // since modules are static, lookups are short-circuited (AnalysisVisitor
  // stores the module name in the AST)
  if (!a->base_module_name.empty()) {
    // technically this only needs to be at Annotated phase, since we only need
    // the global space to be updated with the module's variables. but we don't
    // have a way of specifying import dependencies and we need to make sure the
    // module's globals are written before the code we're generating executes,
    // so we require Imported phase to enforce that.
    auto module = this->global->get_module_at_phase(a->base_module_name,
        ModuleAnalysis::Phase::Imported);
    VariableLocation loc = this->location_for_global(module.get(), a->name);

    // if this is an object, add a reference to it; otherwise just load it
    this->as.write_mov(MemoryReference(this->target_register), loc.mem);
    if (type_has_refcount(loc.type.type)) {
      this->write_add_reference(MemoryReference(this->target_register, 0));
    }

    this->current_type = loc.type;
    if (this->current_type.type == ValueType::Indeterminate) {
      throw compile_error("attribute has Indeterminate type", this->file_offset);
    }

    return;
  }

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

  // if it's an object attribute, store the value and evaluate the object
  if (a->base.get()) {

    // don't touch my value plz
    Register value_register = this->target_register;
    this->reserve_register(this->target_register);
    this->target_register = this->available_register();

    // evaluate the base object
    a->base->accept(this);

    // typecheck the result: it should be an Instance object, the class should
    // have the attribute that we're setting (location_for_attribute will check
    // this) and the attr should be the same type as the value being written
    if (this->current_type.type != ValueType::Instance) {
      throw compile_error("cannot dynamically set attributes on non-instance objects",
          this->file_offset);
    }
    auto* cls = this->global->context_for_class(this->current_type.class_id);
    if (!cls) {
      throw compile_error("object class does not exist", this->file_offset);
    }
    VariableLocation loc = this->location_for_attribute(cls, a->name,
        this->target_register);
    {
      const auto& attr = cls->attributes.at(a->name);
      if (attr.types_equal(this->current_type)) {
        string attr_type = attr.str();
        string new_type = this->current_type.str();
        throw compile_error(string_printf("attribute %s changes type from %s to %s",
            a->name.c_str(), attr_type.c_str(), new_type.c_str()),
            this->file_offset);
      }
    }

    // write the value into the right attribute
    this->as.write_mov(loc.mem, MemoryReference(value_register));

    // clean up
    this->target_register = value_register;
    this->release_register(this->target_register);

  // if a->base is missing, then it's a simple local variable write
  } else {
    VariableLocation loc = this->location_for_variable(a->name);

    // typecheck the result. the type of a variable can only be changed if it's
    // Indeterminate; otherwise it's an error
    // TODO: deduplicate some of this with location_for_variable
    Variable* target_variable = NULL;
    if (loc.is_global) {
      target_variable = &this->module->globals.at(loc.name);
    } else {
      try {
        target_variable = &this->local_overrides.at(loc.name);
      } catch (const out_of_range&) {
        target_variable = &this->target_function->locals.at(loc.name);
      }
    }
    if (!target_variable) {
      throw compile_error("target variable not found");
    }
    if (target_variable->type == ValueType::Indeterminate) {
      *target_variable = current_type;
    } else if (*target_variable != loc.type) {
      string target_type_str = target_variable->str();
      string value_type_str = current_type.str();
      throw compile_error(string_printf("variable %s changes type from %s to %s\n",
          loc.name.c_str(), target_type_str.c_str(),
          value_type_str.c_str()), this->file_offset);
    }

    // now save the value to the appropriate slot
    this->as.write_mov(loc.mem, MemoryReference(target_register));
  }
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
  this->target_register = Register::RAX;
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

  // write a destructor call if necessary
  if (type_has_refcount(this->current_type.type)) {
    this->write_delete_reference(MemoryReference(this->target_register),
        this->current_type);
  }
}

void CompilationVisitor::visit(AssignmentStatement* a) {
  this->file_offset = a->file_offset;

  this->as.write_label(string_printf("__AssignmentStatement_%p", a));

  // unlike in AnalysisVisitor, we look at the lvalue references first, so we
  // can know where to put the resulting values when generating their code

  // TODO: currently we don't support unpacking at all; we only support simple
  // assignments

  // generate code to load the value into any available register
  this->target_register = available_register();
  a->value->accept(this);

  // now write the value into the appropriate slot
  this->as.write_label(string_printf("__AssignmentStatement_%p_write_value", a));
  a->target->accept(this);
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

  // case 3
  if (a->import_star) {
    throw compile_error("import * is not supported", a->file_offset);
  }

  // case 1: import entire modules, not specific names
  if (a->names.empty()) {
    // we actually don't need to do anything here; module lookups are always
    // done statically
    return;
  }

  // case 2: import some names from a module (from x import y)
  const string& module_name = a->modules.begin()->first;
  auto module = this->global->get_module_at_phase(module_name,
      ModuleAnalysis::Phase::Imported);
  MemoryReference target_mem(this->target_register);
  for (const auto& it : a->names) {
    VariableLocation src_loc = this->location_for_global(module.get(), it.first);
    VariableLocation dest_loc = this->location_for_variable(it.second);

    this->as.write_label(string_printf("__ImportStatement_%p_copy_%s_%s",
        a, it.first.c_str(), it.second.c_str()));

    // get the value from the other module
    this->as.write_mov(target_mem, src_loc.mem);

    // if it's an object, add a reference to it
    if (type_has_refcount(module->globals.at(it.first).type)) {
      this->write_add_reference(target_mem);
    }

    // store the value in this module
    this->as.write_mov(dest_loc.mem, target_mem);
  }
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
  this->as.write_label(string_printf("__ReturnStatement_%p_evaluate_expression", a));
  this->target_register = Register::RAX;
  a->value->accept(this);

  // record this return type
  this->function_return_types.emplace(this->current_type);

  // generate a jump to the end of the function (this is right before the
  // relevant destructor calls)
  this->as.write_label(string_printf("__ReturnStatement_%p_return", a));
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

  // get the collection object and save it on the stack
  this->as.write_label(string_printf("__ForStatement_%p_get_collection", a));
  a->collection->accept(this);
  Variable collection_type = this->current_type;
  this->write_push(this->target_register);

  string next_label = string_printf("__ForStatement_%p_next", a);
  string end_label = string_printf("__ForStatement_%p_complete", a);
  string break_label = string_printf("__ForStatement_%p_broken", a);

  // currently this is only implemented for lists
  if (collection_type.type == ValueType::List) {

    // we'll store the item index in rbx
    if (this->target_register == Register::RBX) {
      throw compile_error("cannot use rbx as target register for list iteration");
    }
    this->write_push(Register::RBX);
    MemoryReference rbx_mem(Register::RBX);
    this->as.write_mov(rbx_mem, 0);

    this->as.write_label(next_label);
    // get the list object
    this->as.write_mov(MemoryReference(this->target_register),
        MemoryReference(Register::RSP, 8));
    // check if we're at the end
    this->as.write_cmp(rbx_mem, MemoryReference(this->target_register, 0x10));
    // if we are, skip the loop body
    this->as.write_jge(end_label);
    // else, get the list items
    this->as.write_mov(MemoryReference(this->target_register),
        MemoryReference(this->target_register, 0x20));
    // get the current item from the list
    this->as.write_mov(MemoryReference(this->target_register),
        MemoryReference(this->target_register, 0, Register::RBX, 8));
    // increment the item index
    this->as.write_inc(rbx_mem);

    // load the value into the correct local variable slot
    this->as.write_label(string_printf("__ForStatement_%p_write_value", a));
    this->current_type = collection_type.extension_types[0];
    a->variable->accept(this);

    // now do the loop body
    this->as.write_label(string_printf("__ForStatement_%p_body", a));
    this->visit_list(a->items);
    this->as.write_jmp(next_label);
    this->as.write_label(end_label);

    // restore rbx
    this->write_pop(Register::RBX);

  } else {
    throw compile_error("iteration not implemented for " + collection_type.str(),
        this->file_offset);
  }

  // if there's an else statement, generate the body here
  if (a->else_suite.get()) {
    a->else_suite->accept(this);
  }

  // any break statement will jump over the loop body and the else statement
  this->as.write_label(break_label);

  // we still own a reference to the collection; destroy it now
  // note: all collection types have refcounts; dunno why I bothered checking
  if (type_has_refcount(collection_type.type)) {
    this->write_pop(this->target_register);
    this->write_delete_reference(MemoryReference(this->target_register),
        collection_type);
  } else {
    this->as.write_add(MemoryReference(Register::RSP), 8);
  }
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
  this->as.write_label(string_printf("__WhileStatement_%p_body", a));
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
  this->target_register = Register::RAX;
  this->RecursiveASTVisitor::visit(a);
  this->write_function_cleanup(base_label);
}

void CompilationVisitor::visit(ClassDefinition* a) {
  this->file_offset = a->file_offset;

  // write the class' context to the variable
  auto* cls = this->global->context_for_class(a->class_id);
  auto loc = this->location_for_variable(a->name);
  this->as.write_label(string_printf("__ClassDefinition_%p_assign", a));
  this->as.write_mov(this->target_register, reinterpret_cast<int64_t>(cls));
  this->as.write_mov(loc.mem, MemoryReference(this->target_register));

  // create the class destructor function
  if (!cls->destructor) {
    // if none of the class attributes have destructors, then the overall class
    // destructor trivializes to free()
    bool has_subdestructors = false;
    for (const auto& it : cls->attributes) {
      if (type_has_refcount(it.second.type)) {
        has_subdestructors = true;
        break;
      }
    }

    // no subdestructors; just use free()
    if (!has_subdestructors) {
      cls->destructor = reinterpret_cast<void*>(&free);
      if (this->global->debug_flags & DebugFlag::Assembly) {
        fprintf(stderr, "[%s:%" PRId64 "] class has trivial destructor\n",
            a->name.c_str(), a->class_id);
      }

    // there are subdestructors; have to write a destructor function
    } else {
      string base_label = string_printf("__ClassDefinition_%p_destructor", a);
      AMD64Assembler dtor_as;
      dtor_as.write_label(base_label);

      // lead-in (stack frame setup)
      dtor_as.write_push(Register::RBP);
      dtor_as.write_mov(MemoryReference(Register::RBP),
          MemoryReference(Register::RSP));

      // we'll keep the object ptr in rbx since it's callee-save
      dtor_as.write_push(Register::RBX);
      dtor_as.write_mov(MemoryReference(Register::RBX),
          MemoryReference(Register::RDI));

      // make sure the stack is aligned at call time for any subfunctions
      dtor_as.write_sub(MemoryReference(Register::RSP), 8);

      // the first 2 fields are the refcount and destructor pointer
      // the rest are the attributes, in the same order as in the attributes map
      size_t offset = 16;
      for (const auto& it : cls->attributes) {
        if (type_has_refcount(it.second.type)) {
          // write a destructor call
          dtor_as.write_label(string_printf("%s_destroy_%s", base_label.c_str(),
              it.first.c_str()));
          dtor_as.write_mov(MemoryReference(Register::RDI),
              MemoryReference(Register::RBX, offset));
          dtor_as.write_mov(MemoryReference(Register::RAX),
              MemoryReference(RDI, 8));
          dtor_as.write_call(MemoryReference(Register::RAX));
        }
        offset += 8;
      }

      // cheating time: "return" by jumping directly to free() so it will return
      // to the caller
      dtor_as.write_label(base_label + "_return");
      dtor_as.write_add(MemoryReference(Register::RSP), 8);
      dtor_as.write_pop(Register::RBX);
      dtor_as.write_mov(Register::RBP, reinterpret_cast<int64_t>(&free));
      dtor_as.write_xchg(Register::RBP, MemoryReference(Register::RSP));
      dtor_as.write_ret();

      // assemble it
      multimap<size_t, string> compiled_labels;
      string compiled = dtor_as.assemble(&compiled_labels);
      cls->destructor = this->global->code.append(compiled);
      this->module->compiled_size += compiled.size();

      if (this->global->debug_flags & DebugFlag::Assembly) {
        fprintf(stderr, "[%s:%" PRId64 "] class destructor assembled\n",
            a->name.c_str(), a->class_id);
        uint64_t addr = reinterpret_cast<uint64_t>(cls->destructor);
        print_data(stderr, compiled.data(), compiled.size(), addr);
        string disassembly = AMD64Assembler::disassemble(compiled.data(),
            compiled.size(), addr, &compiled_labels);
        fprintf(stderr, "\n%s\n", disassembly.c_str());
      }
    }
  }
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
      // TODO: this is dumb; we can probably optimize this to eliminate some
      // memory accesses here. we have to preserve the value in rax somehow but
      // we can't easily use the stack since we're popping locals off of it
      this->as.write_xchg(Register::RAX, MemoryReference(Register::RSP, 0));
      this->write_delete_reference(MemoryReference(Register::RAX), it->second);
      this->write_pop(Register::RAX);
    } else {
      // no destructor; just skip it
      // TODO: we can coalesce these adds for great justice
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

CompilationVisitor::VariableLocation CompilationVisitor::location_for_global(
    ModuleAnalysis* module, const string& name) {
  VariableLocation loc;
  loc.name = name;

  // if we're writing a global, use its global slot offset (from R13)
  auto it = module->globals.find(name);
  if (it == module->globals.end()) {
    throw compile_error("nonexistent global: " + name, this->file_offset);
  }
  ssize_t offset = distance(module->globals.begin(), it);
  loc.is_global = true;
  loc.type = it->second;
  loc.mem = MemoryReference(Register::R13,
      offset * sizeof(int64_t) + module->global_base_offset);

  return loc;
}

CompilationVisitor::VariableLocation CompilationVisitor::location_for_variable(
    const string& name) {

  // if we're writing a global, use its global slot offset (from R13)
  if (!this->target_function || !this->target_function->locals.count(name)) {
    return this->location_for_global(this->module, name);
  }

  // if we're writing a local, use its local slot offset (from RBP)
  auto it = this->target_function->locals.find(name);
  if (it == this->target_function->locals.end()) {
    throw compile_error("nonexistent local: " + name, this->file_offset);
  }

  VariableLocation loc;
  loc.name = name;
  loc.is_global = false;
  loc.mem = MemoryReference(Register::RBP, sizeof(int64_t) * (-static_cast<ssize_t>(1 + distance(this->target_function->locals.begin(), it))));

  // use the argument type if given
  try {
    loc.type = this->local_overrides.at(name);
  } catch (const out_of_range&) {
    loc.type = it->second;
  }

  return loc;
}

CompilationVisitor::VariableLocation CompilationVisitor::location_for_attribute(
    ClassContext* cls, const string& name, Register instance_reg) {

  auto it = cls->attributes.find(name);
  if (it == cls->attributes.end()) {
    throw compile_error("nonexistent attribute: " + name, this->file_offset);
  }

  // attributes are stored at [instance_reg + 8 * which + 16]
  VariableLocation loc;
  loc.name = name;
  loc.is_global = false;
  loc.mem = MemoryReference(instance_reg,
      sizeof(int64_t) * (2 + distance(cls->attributes.begin(), it)));
  loc.type = it->second;
  return loc;
}
