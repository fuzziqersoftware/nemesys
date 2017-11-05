#include "CompilationVisitor.hh"

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>

#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>

#include "CommonObjects.hh"
#include "Exception.hh"
#include "BuiltinFunctions.hh"
#include "Debug.hh"
#include "PythonLexer.hh"
#include "PythonParser.hh"
#include "PythonASTNodes.hh"
#include "PythonASTVisitor.hh"
#include "Environment.hh"
#include "AMD64Assembler.hh"
#include "Types/Reference.hh"
#include "Types/Strings.hh"
#include "Types/Format.hh"
#include "Types/List.hh"
#include "Types/Tuple.hh"
#include "Types/Dictionary.hh"

using namespace std;



/* in general we follow the system v calling convention:
 * - int arguments in rdi, rsi, rdx, rcx, r8, r9 (in that order); more on stack
 * - float arguments in xmm0-7 (in that order); more floats on stack
 * - some special handling for variadic functions (need to look this up)
 * - return in rax (+rdx for 128-bit returns), and xmm0 (+xmm1 if needed)
 *
 * space for all local variables is initialized at the beginning of the
 * function's scope. temporary variables may only live during a statement's
 * execution; when a statement is completed, they are either copied to a
 * local/global variable or destroyed.
 *
 * the nemesys calling convention is a little more complex than the system v
 * convention, but is mostly compatible with it, so nemesys functions can
 * directly call c functions (e.g. built-in functions in nemesys itself). the
 * nemesys register assignment is as follows:
 *   reg     = callee-save? = purpose
 *   RAX     =      no      = int return value
 *   RCX     =      no      = 4th int arg
 *   RDX     =      no      = 3rd int arg, int return value (high)
 *   RBX     =      yes     = unused
 *   RSP     =              = stack pointer
 *   RBP     =      yes     = frame pointer
 *   RSI     =      no      = 2nd int arg
 *   RDI     =      no      = 1st int arg
 *   R8      =      no      = 5th int arg
 *   R9      =      no      = 6th int arg
 *   R10     =      no      = temp values
 *   R11     =      no      = temp values
 *   R12     =      yes     = common object pointer
 *   R13     =      yes     = global space pointer
 *   R14     =      yes     = exception block pointer
 *   R15     =      yes     = active exception instance
 *   XMM0    =      no      = 1st float arg, float return value
 *   XMM1    =      no      = 2st float arg, float return value (high)
 *   XMM2-7  =      no      = 3rd-8th float args (in register order)
 *   XMM8-15 =      no      = temp values
 *
 * the special registers (r12-r15) are used as follows:
 * - common objects are stored in a statically-allocated array pointed to by
 *   r12. this array contains handy stuff like pointers to malloc() and free(),
 *   the preallocated MemoryError singleton instance, etc.
 * - global variables are referenced by offsets from r13. each module has a
 *   statically-assigned space above r13, and should read/write globals with
 *   e.g. `mov [r13 + X]` opcodes.
 * - the active exception block pointer is stored in r14. except ad the very
 *   beginning and end of module root scope functions, r14 must never be NULL.
 *   the exception block defines where control should return to when an
 *   exception is raised. this includes all `except Something` blocks, but also
 *   all `finally` blocks and all function scopes (so destructors are properly
 *   called). see the definition of the ExceptionBlock structure in Exception.hh
 *   for more information.
 * - the active exception object is stored in r15. most of the time this should
 *   be NULL; it's only non-NULL when a raise statement is being executed and is
 *   in the proces of transferring control to an except block, or when a finally
 *   block or function destructor call block is running and there is an active
 *   exception.
 *
 * module root scopes compile into functions that take no arguments and return
 * the active exception object if one was raised, or NULL if no exception was
 * raised. functions defined within modules expect r12-r14 to already be set up
 * properly.
 */

static const vector<Register> int_argument_register_order = {
  Register::RDI, Register::RSI, Register::RDX, Register::RCX, Register::R8,
  Register::R9,
};
static const vector<Register> float_argument_register_order = {
  Register::XMM0, Register::XMM1, Register::XMM2, Register::XMM3,
  Register::XMM4, Register::XMM5, Register::XMM6, Register::XMM7,
};

static const int64_t default_available_int_registers =
    (1 << Register::RAX) |
    (1 << Register::RCX) |
    (1 << Register::RDX) |
    (1 << Register::RSI) |
    (1 << Register::RDI) |
    (1 << Register::R8) |
    (1 << Register::R9) |
    (1 << Register::R10) |
    (1 << Register::R11);
static const int64_t default_available_float_registers = 0xFFFF; // all of them



CompilationVisitor::CompilationVisitor(GlobalAnalysis* global,
    ModuleAnalysis* module, int64_t target_function_id, int64_t target_split_id,
    const unordered_map<string, Variable>* local_overrides) : file_offset(-1),
    global(global), module(module),
    target_function(this->global->context_for_function(target_function_id)),
    target_split_id(target_split_id),
    available_int_registers(default_available_int_registers),
    available_float_registers(default_available_float_registers),
    target_register(Register::None), float_target_register(Register::XMM0),
    stack_bytes_used(0), holding_reference(false),
    evaluating_instance_pointer(false), in_finally_block(false) {

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



string CompilationVisitor::VariableLocation::str() const {
  string type_str = this->type.str();
  string mem_str = this->mem.str(OperandSize::QuadWord);
  return string_printf("%s (%s) = %s @ %s", this->name.c_str(),
      this->is_global ? "global" : "nonglobal", type_str.c_str(),
      mem_str.c_str());
}



Register CompilationVisitor::reserve_register(Register which,
    bool float_register) {
  if (which == Register::R15) {
    throw invalid_argument("r15");
  }

  if (which == Register::None) {
    which = this->available_register(Register::None, float_register);
  }

  int32_t* available_mask = float_register ? &this->available_float_registers :
      &this->available_int_registers;
  if (!(*available_mask & (1 << which))) {
    throw compile_error(string_printf("register %s is not available", name_for_register(which)), this->file_offset);
  }
  *available_mask &= ~(1 << which);
  return which;
}

void CompilationVisitor::release_register(Register which, bool float_register) {
  int32_t* available_mask = float_register ? &this->available_float_registers :
      &this->available_int_registers;
  *available_mask |= (1 << which);
}

void CompilationVisitor::release_all_registers(bool float_registers) {
  if (float_registers) {
    this->available_float_registers = default_available_float_registers;
  } else {
    this->available_int_registers = default_available_int_registers;
  }
}

Register CompilationVisitor::available_register(Register preferred,
    bool float_register) {
  int32_t* available_mask = float_register ? &this->available_float_registers :
      &this->available_int_registers;

  if (*available_mask & (1 << preferred)) {
    return preferred;
  }

  Register which;
  for (which = Register::RAX; // = 0
       !(*available_mask & (1 << which)) && (static_cast<int64_t>(which) < Register::Count);
       which = static_cast<Register>(static_cast<int64_t>(which) + 1));
  if (static_cast<int64_t>(which) >= Register::Count) {
    throw compile_error("no registers are available", this->file_offset);
  }
  return which;
}

bool CompilationVisitor::register_is_available(Register which,
    bool float_register) {
  int32_t* available_mask = float_register ? &this->available_float_registers :
      &this->available_int_registers;
  return *available_mask & (1 << which);
}

Register CompilationVisitor::available_register_except(
    const vector<Register>& prevented, bool float_register) {
  int32_t* available_mask = float_register ? &this->available_float_registers :
      &this->available_int_registers;

  int32_t prevented_mask = 0;
  for (Register r : prevented) {
    prevented_mask |= (1 << r);
  }

  Register which;
  for (which = Register::RAX; // = 0
       ((prevented_mask & (1 << which)) || !(*available_mask & (1 << which))) &&
       (static_cast<int64_t>(which) < Register::Count);
       which = static_cast<Register>(static_cast<int64_t>(which) + 1));
  if (static_cast<int64_t>(which) >= Register::Count) {
    throw compile_error("no registers are available", this->file_offset);
  }
  return which;
}

int64_t CompilationVisitor::write_push_reserved_registers() {
  // push int registers
  Register which;
  for (which = Register::RAX; // = 0
      static_cast<int64_t>(which) < Register::Count;
       which = static_cast<Register>(static_cast<int64_t>(which) + 1)) {
    if (!(default_available_int_registers & (1 << which))) {
      continue; // this register isn't used by CompilationVisitor
    }
    if (this->available_int_registers & (1 << which)) {
      continue; // this register is used but not reserved
    }

    this->write_push(which);
  }

  // push xmm registers
  for (which = Register::XMM0; // = 0
      static_cast<int64_t>(which) < Register::Count;
       which = static_cast<Register>(static_cast<int64_t>(which) + 1)) {
    if (!(default_available_float_registers & (1 << which))) {
      continue; // this register isn't used by CompilationVisitor
    }
    if (this->available_float_registers & (1 << which)) {
      continue; // this register is used but not reserved
    }

    this->adjust_stack(-8);
    this->as.write_movsd(MemoryReference(Register::RSP, 0),
        MemoryReference(which));
  }

  // reset the available flags and return the old flags
  int64_t ret = this->available_registers;
  this->available_int_registers = default_available_int_registers;
  this->available_float_registers = default_available_float_registers;
  return ret;
}

void CompilationVisitor::write_pop_reserved_registers(int64_t mask) {
  if ((this->available_int_registers != default_available_int_registers) ||
      (this->available_float_registers != default_available_float_registers)) {
    throw compile_error("some registers were not released when reserved were popped", this->file_offset);
  }

  this->available_registers = mask;

  Register which;
  for (which = Register::XMM15;
      static_cast<int64_t>(which) > Register::None;
       which = static_cast<Register>(static_cast<int64_t>(which) - 1)) {
    if (!(default_available_float_registers & (1 << which))) {
      continue; // this register isn't used by CompilationVisitor
    }
    if (this->available_float_registers & (1 << which)) {
      continue; // this register is used but not reserved
    }

    this->as.write_movsd(MemoryReference(which), MemoryReference(Register::RSP, 0));
    this->adjust_stack(8);
  }

  for (which = Register::R15;
      static_cast<int64_t>(which) > Register::None;
       which = static_cast<Register>(static_cast<int64_t>(which) - 1)) {
    if (!(default_available_int_registers & (1 << which))) {
      continue; // this register isn't used by CompilationVisitor
    }
    if (this->available_int_registers & (1 << which)) {
      continue; // this register is used but not reserved
    }

    this->write_pop(which);
  }
}



void CompilationVisitor::visit(UnaryOperation* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  this->as.write_label(string_printf("__UnaryOperation_%p_evaluate", a));

  // generate code for the value expression
  a->expr->accept(this);

  if (this->current_type.type == ValueType::Indeterminate) {
    throw compile_error("operand has Indeterminate type", this->file_offset);
  }

  // apply the unary operation on top of the result
  // we can use the same target register
  MemoryReference target_mem(this->target_register);
  this->as.write_label(string_printf("__UnaryOperation_%p_apply", a));
  switch (a->oper) {

    case UnaryOperator::LogicalNot:
      if (this->current_type.type == ValueType::None) {
        // `not None` is always true
        this->as.write_mov(this->target_register, 1);

      } else if (this->current_type.type == ValueType::Bool) {
        // bools are either 0 or 1; just flip it
        this->as.write_xor(target_mem, 1);

      } else if (this->current_type.type == ValueType::Int) {
        // check if the value is zero
        this->as.write_test(target_mem, target_mem);
        this->as.write_mov(this->target_register, 0);
        this->as.write_setz(MemoryReference(byte_register_for_register(
            this->target_register)));

      } else if (this->current_type.type == ValueType::Float) {
        // 0.0 and -0.0 are falsey, everything else is truthy
        // the sign bit is the highest bit; to truth-test floats, we just shift
        // out the sign bit and check if the rest is zero
        this->as.write_movq_from_xmm(target_mem, this->float_target_register);
        this->as.write_shl(target_mem, 1);
        this->as.write_test(target_mem, target_mem);
        this->as.write_mov(this->target_register, 0);
        this->as.write_setz(MemoryReference(byte_register_for_register(
            this->target_register)));

      } else if ((this->current_type.type == ValueType::Bytes) ||
                 (this->current_type.type == ValueType::Unicode) ||
                 (this->current_type.type == ValueType::List) ||
                 (this->current_type.type == ValueType::Tuple) ||
                 (this->current_type.type == ValueType::Set) ||
                 (this->current_type.type == ValueType::Dict)) {
        // load the size field, check if it's zero
        Register reg = this->available_register_except({this->target_register});
        MemoryReference mem(reg);
        this->reserve_register(reg);
        this->as.write_mov(mem, MemoryReference(this->target_register, 0x10));
        // if we're holding a reference to the object, release it
        this->write_delete_held_reference(target_mem);
        this->as.write_test(mem, mem);
        this->as.write_mov(this->target_register, 0);
        this->as.write_setz(MemoryReference(byte_register_for_register(
            this->target_register)));
        this->release_register(reg);

      } else {
        // other types cannot be falsey
        this->as.write_mov(this->target_register, 1);
        this->write_delete_held_reference(target_mem);
      }

      this->current_type = Variable(ValueType::Bool);
      break;

    case UnaryOperator::Not:
      if ((this->current_type.type == ValueType::Int) ||
          (this->current_type.type == ValueType::Bool)) {
        this->as.write_not(target_mem);
      } else {
        throw compile_error("bitwise not can only be applied to ints and bools", this->file_offset);
      }
      this->current_type = Variable(ValueType::Int);
      break;

    case UnaryOperator::Positive:
      // the + operator converts bools into ints; leaves ints and floats alone
      if (this->current_type.type == ValueType::Bool) {
        this->current_type = Variable(ValueType::Int);
      } else if ((this->current_type.type != ValueType::Int) &&
                 (this->current_type.type != ValueType::Float)) {
        throw compile_error("arithmetic positive can only be applied to numeric values", this->file_offset);
      }
      break;

    case UnaryOperator::Negative:
      if ((this->current_type.type == ValueType::Bool) ||
          (this->current_type.type == ValueType::Int)) {
        this->as.write_neg(target_mem);
        this->current_type = Variable(ValueType::Int);

      } else if (this->current_type.type == ValueType::Float) {
        // this is totally cheating. here we manually flip the sign bit
        // TODO: there's probably a not-stupid way to do this
        MemoryReference tmp(this->available_register());
        this->as.write_movq_from_xmm(tmp, this->float_target_register);
        this->as.write_rol(tmp, 1);
        this->as.write_xor(tmp, 1);
        this->as.write_ror(tmp, 1);
        this->as.write_movq_to_xmm(this->float_target_register, tmp);

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

void CompilationVisitor::write_current_truth_value_test() {

  MemoryReference target_mem((this->current_type.type == ValueType::Float) ?
      this->float_target_register : this->target_register);
  switch (this->current_type.type) {
    case ValueType::Indeterminate:
      throw compile_error("truth value test on Indeterminate type", this->file_offset);
    case ValueType::ExtensionTypeReference:
      throw compile_error("truth value test on ExtensionTypeReference type", this->file_offset);

    case ValueType::Bool:
    case ValueType::Int:
      this->as.write_test(target_mem, target_mem);
      break;

    case ValueType::Float: {
      // 0.0 and -0.0 are falsey, everything else is truthy
      // the sign bit is the highest bit; to truth-test floats, we just shift
      // out the sign bit and check if the rest is zero
      MemoryReference tmp(this->available_register());
      this->as.write_movq_from_xmm(tmp, this->float_target_register);
      this->as.write_shl(tmp, 1);
      this->as.write_test(tmp, tmp);
      break;
    }

    case ValueType::Bytes:
    case ValueType::Unicode:
    case ValueType::List:
    case ValueType::Tuple:
    case ValueType::Set:
    case ValueType::Dict: {
      // we have to use a register for this
      MemoryReference size_mem(this->available_register_except(
          {this->target_register}));
      this->as.write_mov(size_mem, MemoryReference(this->target_register, 0x10));
      this->as.write_test(size_mem, size_mem);
      break;
    }

    case ValueType::None:
    case ValueType::Function:
    case ValueType::Class:
    case ValueType::Instance:
    case ValueType::Module: {
      string type_str = this->current_type.str();
      throw compile_error(string_printf(
          "cannot generate truth test for %s value", type_str.c_str()),
          this->file_offset);
    }
  }
}

void CompilationVisitor::visit(BinaryOperation* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  MemoryReference target_mem(this->target_register);
  MemoryReference float_target_mem(this->float_target_register);

  // LogicalOr and LogicalAnd may not evaluate the right-side operand, so we
  // have to implement those separately (the other operators evaluate both
  // operands in all cases)
  if ((a->oper == BinaryOperator::LogicalOr) ||
      (a->oper == BinaryOperator::LogicalAnd)) {
    // generate code for the left value
    this->as.write_label(string_printf("__BinaryOperation_%p_evaluate_left", a));
    a->left->accept(this);
    if (type_has_refcount(this->current_type.type) && !this->holding_reference) {
      throw compile_error("non-held reference to left binary operator argument",
          this->file_offset);
    }

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

    // for LogicalOr, use the left value if it's nonzero and use the right value
    // otherwise; for LogicalAnd, do the opposite
    string label_name = string_printf("BinaryOperation_%p_evaluate_right", a);
    this->write_current_truth_value_test();
    if (a->oper == BinaryOperator::LogicalOr) {
      this->as.write_jnz(label_name); // skip right if left truthy
    } else { // LogicalAnd
      this->as.write_jz(label_name); // skip right if left falsey
    }

    // if we get here, then the right-side value is the one that will be
    // returned; delete the reference we may be holding to the left-side value
    this->write_delete_held_reference(MemoryReference(this->target_register));

    // generate code for the right value
    Variable left_type = move(this->current_type);
    a->right->accept(this);
    this->as.write_label(label_name);
    if (type_has_refcount(this->current_type.type) && !this->holding_reference) {
      throw compile_error("non-held reference to right binary operator argument",
          this->file_offset);
    }

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
  Variable left_type = move(this->current_type);
  if (left_type.type == ValueType::Float) {
    this->as.write_movq_from_xmm(target_mem, this->float_target_register);
  }
  this->write_push(this->target_register); // so right doesn't clobber it
  bool left_holding_reference = type_has_refcount(this->current_type.type);
  if (left_holding_reference && !this->holding_reference) {
    throw compile_error("non-held reference to left binary operator argument",
        this->file_offset);
  }

  this->as.write_label(string_printf("__BinaryOperation_%p_evaluate_right", a));
  a->right->accept(this);
  Variable& right_type = this->current_type;
  if (right_type.type == ValueType::Float) {
    this->as.write_movq_from_xmm(target_mem, this->float_target_register);
  }
  this->write_push(this->target_register); // for the destructor call later
  bool right_holding_reference = type_has_refcount(this->current_type.type);
  if (right_holding_reference && !this->holding_reference) {
    throw compile_error("non-held reference to right binary operator argument",
        this->file_offset);
  }

  MemoryReference left_mem(Register::RSP, 8);
  MemoryReference right_mem(Register::RSP, 0);

  // pick a temporary register that isn't the target register
  MemoryReference temp_mem(this->available_register_except({this->target_register}));

  bool left_int_only = (left_type.type == ValueType::Int);
  bool right_int_only = (right_type.type == ValueType::Int);
  bool left_int = left_int_only || (left_type.type == ValueType::Bool);
  bool right_int = right_int_only || (right_type.type == ValueType::Bool);
  bool left_float = (left_type.type == ValueType::Float);
  bool right_float = (right_type.type == ValueType::Float);
  bool left_numeric = left_int || left_float;
  bool right_numeric = right_int || right_float;
  bool left_bytes = (left_type.type == ValueType::Bytes);
  bool right_bytes = (right_type.type == ValueType::Bytes);
  bool left_unicode = (left_type.type == ValueType::Unicode);
  bool right_unicode = (right_type.type == ValueType::Unicode);
  bool right_tuple = (right_type.type == ValueType::Tuple);

  this->as.write_label(string_printf("__BinaryOperation_%p_combine", a));
  switch (a->oper) {
    case BinaryOperator::LessThan:
    case BinaryOperator::GreaterThan:
    case BinaryOperator::LessOrEqual:
    case BinaryOperator::GreaterOrEqual:

      // it's an error to ordered-compare disparate types to each other, except
      // for numeric types
      if ((!left_numeric || !right_numeric) && (left_type.type != right_type.type)) {
        throw compile_error("cannot perform ordered comparison between " +
            left_type.str() + " and " + right_type.str(), this->file_offset);
      }

    case BinaryOperator::Equality:
    case BinaryOperator::NotEqual:
      if (left_numeric && right_numeric) {
        Register xmm = this->available_register(Register::None, true);
        MemoryReference xmm_mem(xmm);
        this->as.write_xor(target_mem, target_mem);

        // Int vs Int
        if (left_int && right_int) {
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

        // Float vs Int
        } else if (left_float && right_int) {
          this->as.write_cvtsi2sd(xmm, right_mem);

          // we're comparing in the opposite direction, so we negate the results
          // of ordered comparisons
          if (a->oper == BinaryOperator::LessThan) {
            this->as.write_cmpnltsd(xmm, left_mem);
          } else if (a->oper == BinaryOperator::GreaterThan) {
            this->as.write_cmplesd(xmm, left_mem);
          } else if (a->oper == BinaryOperator::LessOrEqual) {
            this->as.write_cmpnlesd(xmm, left_mem);
          } else if (a->oper == BinaryOperator::GreaterOrEqual) {
            this->as.write_cmpltsd(xmm, left_mem);
          } else if (a->oper == BinaryOperator::Equality) {
            this->as.write_cmpeqsd(xmm, left_mem);
          } else if (a->oper == BinaryOperator::NotEqual) {
            this->as.write_cmpneqsd(xmm, left_mem);
          }
          this->as.write_movq_from_xmm(target_mem, xmm);

        // Int vs Float and Float vs Float
        } else if (right_float) {
          if (left_int) {
            this->as.write_cvtsi2sd(xmm, left_mem);
          } else {
            this->as.write_movsd(xmm_mem, left_mem);
          }
          if (a->oper == BinaryOperator::LessThan) {
            this->as.write_cmpltsd(xmm, right_mem);
          } else if (a->oper == BinaryOperator::GreaterThan) {
            this->as.write_cmpnlesd(xmm, right_mem);
          } else if (a->oper == BinaryOperator::LessOrEqual) {
            this->as.write_cmplesd(xmm, right_mem);
          } else if (a->oper == BinaryOperator::GreaterOrEqual) {
            this->as.write_cmpnltsd(xmm, right_mem);
          } else if (a->oper == BinaryOperator::Equality) {
            this->as.write_cmpeqsd(xmm, right_mem);
          } else if (a->oper == BinaryOperator::NotEqual) {
            this->as.write_cmpneqsd(xmm, right_mem);
          }
          this->as.write_movq_from_xmm(target_mem, xmm);

        } else {
          throw compile_error("unimplemented numeric ordered comparison: " +
              left_type.str() + " vs " + right_type.str(), this->file_offset);
        }

      } else if ((left_bytes && right_bytes) || (left_unicode && right_unicode)) {

        if ((a->oper == BinaryOperator::Equality) ||
            (a->oper == BinaryOperator::NotEqual)) {
          const MemoryReference target_function = common_object_reference(
              left_bytes ? void_fn_ptr(&bytes_equal) : void_fn_ptr(&unicode_equal));
          this->write_function_call(target_function, {target_mem, left_mem},
              {}, -1, this->target_register);
          if (a->oper == BinaryOperator::NotEqual) {
            this->as.write_xor(target_mem, 1);
          }

        } else {
          const MemoryReference target_function = common_object_reference(
              left_bytes ? void_fn_ptr(&bytes_compare) : void_fn_ptr(&unicode_compare));
          this->write_function_call(target_function, {left_mem, right_mem},
              {}, -1, this->target_register);
          this->as.write_cmp(target_mem, 0);
          this->as.write_mov(target_mem, 0);
          target_mem.base_register = byte_register_for_register(target_mem.base_register);
          if (a->oper == BinaryOperator::LessThan) {
            this->as.write_setl(target_mem);
          } else if (a->oper == BinaryOperator::GreaterThan) {
            this->as.write_setg(target_mem);
          } else if (a->oper == BinaryOperator::LessOrEqual) {
            this->as.write_setle(target_mem);
          } else if (a->oper == BinaryOperator::GreaterOrEqual) {
            this->as.write_setge(target_mem);
          }
        }

      } else {
        throw compile_error("unimplemented non-numeric ordered comparison: " +
            left_type.str() + " vs " + right_type.str(), this->file_offset);
      }

      this->current_type = Variable(ValueType::Bool);
      break;

    case BinaryOperator::In:
    case BinaryOperator::NotIn:
      if ((left_bytes && right_bytes) || (left_unicode && right_unicode)) {
        const MemoryReference target_function = common_object_reference(
            left_bytes ? void_fn_ptr(&bytes_contains) : void_fn_ptr(&unicode_contains));
        this->write_function_call(target_function, {target_mem, left_mem}, {},
            -1, this->target_register);
      } else {
        // TODO
        throw compile_error("In/NotIn not yet implemented for " + left_type.str() + " and " + right_type.str(), this->file_offset);
      }
      if (a->oper == BinaryOperator::NotIn) {
        this->as.write_xor(target_mem, 1);
      }
      this->current_type = Variable(ValueType::Bool);
      this->holding_reference = false;
      break;

    case BinaryOperator::Is:
    case BinaryOperator::IsNot: {
      bool negate = (a->oper == BinaryOperator::IsNot);
      // if the left and right types don't match, the result is always false
      if (left_type.type != right_type.type) {
        if (negate) {
          this->as.write_mov(this->target_register, 1);
        } else {
          this->as.write_xor(target_mem, target_mem);
        }

      // None has only one value, so `None is None` is always true
      } else if (left_type.type == ValueType::None) {
        if (negate) {
          this->as.write_xor(target_mem, target_mem);
        } else {
          this->as.write_mov(this->target_register, 1);
        }

      // for everything else, just compare the values directly. this exact same
      // code works for bools and all other types, since their values are
      // pointers and we just need to compare the pointers to know if they're
      // the same object. note that this violates the python standard for
      // integers and floats - these aren't objects at all in nemesys, so the
      // operator isn't well-defined for them; we just do the same as ==/!=.
      } else {
        this->as.write_xor(target_mem, target_mem);
        this->as.write_mov(temp_mem, left_mem);
        this->as.write_cmp(temp_mem, right_mem);
        target_mem.base_register = byte_register_for_register(target_mem.base_register);
        if (negate) {
          this->as.write_setne(target_mem);
        } else {
          this->as.write_sete(target_mem);
        }
      }

      this->current_type = Variable(ValueType::Bool);
      break;
    }

    case BinaryOperator::Or:
      if (left_int && right_int) {
        this->as.write_or(target_mem, left_mem);
        break;
      }
      throw compile_error("Or not valid for " + left_type.str() + " and " + right_type.str(), this->file_offset);

    case BinaryOperator::And:
      if (left_int && right_int) {
        this->as.write_and(target_mem, left_mem);
        break;
      }
      throw compile_error("And not valid for " + left_type.str() + " and " + right_type.str(), this->file_offset);

    case BinaryOperator::Xor:
      if (left_int && right_int) {
        this->as.write_xor(target_mem, left_mem);
        break;
      }
      throw compile_error("Xor not valid for " + left_type.str() + " and " + right_type.str(), this->file_offset);

    case BinaryOperator::LeftShift:
    case BinaryOperator::RightShift:
      if (left_int && right_int) {
        // we can only use cl apparently
        if (this->reserve_register(Register::RCX) != Register::RCX) {
          throw compile_error("RCX not available for shift operation", this->file_offset);
        }
        this->release_register(Register::RCX);
        this->as.write_mov(rcx, target_mem);
        this->as.write_mov(target_mem, left_mem);
        if (a->oper == BinaryOperator::LeftShift) {
          this->as.write_shl_cl(target_mem);
        } else {
          this->as.write_sar_cl(target_mem);
        }
        break;
      }
      throw compile_error("bit shift not valid for " + left_type.str() + " and " + right_type.str(), this->file_offset);

    case BinaryOperator::Addition:
      if ((left_type.type == ValueType::Bytes) &&
          (right_type.type == ValueType::Bytes)) {
        this->write_function_call(common_object_reference(void_fn_ptr(&bytes_concat)),
            {left_mem, target_mem, r14}, {}, -1, this->target_register);

      } else if ((left_type.type == ValueType::Unicode) &&
                 (right_type.type == ValueType::Unicode)) {
        this->write_function_call(common_object_reference(void_fn_ptr(&unicode_concat)),
            {left_mem, target_mem, r14}, {}, -1, this->target_register);

      } else if (left_int && right_int) {
        this->as.write_add(target_mem, left_mem);

      } else if (left_int && right_float) {
        this->as.write_cvtsi2sd(this->float_target_register, left_mem);
        this->as.write_addsd(this->float_target_register, right_mem);

      } else if (left_float && right_int) {
        // the int value is still in the target register; skip the memory access
        this->as.write_cvtsi2sd(this->float_target_register, target_mem);
        this->as.write_addsd(this->float_target_register, left_mem);

        // watch it: in this case the type is different from right_type
        this->current_type = Variable(ValueType::Float);

      } else if (left_float && right_float) {
        this->as.write_addsd(this->float_target_register, left_mem);

      } else {
        throw compile_error("Addition not implemented for " + left_type.str() + " and " + right_type.str(), this->file_offset);
      }
      break;

    case BinaryOperator::Subtraction:
      if (left_int && right_int) {
        this->as.write_neg(target_mem);
        this->as.write_add(target_mem, left_mem);

      } else if (left_int && right_float) {
        this->as.write_cvtsi2sd(this->float_target_register, left_mem);
        this->as.write_subsd(this->float_target_register, right_mem);

      } else if (left_float && right_int) {
        this->as.write_neg(target_mem);
        this->as.write_cvtsi2sd(this->float_target_register, target_mem);
        this->as.write_addsd(this->float_target_register, left_mem);

        // watch it: in this case the type is different from right_type
        this->current_type = Variable(ValueType::Float);

      } else if (left_float && right_float) {
        this->as.write_movq_to_xmm(this->float_target_register, left_mem);
        this->as.write_subsd(this->float_target_register, right_mem);

      } else {
        throw compile_error("Subtraction not implemented for " + left_type.str() + " and " + right_type.str(), this->file_offset);
      }
      break;

    case BinaryOperator::Multiplication:
      if (left_int && right_int) {
        this->as.write_imul(target_mem.base_register, left_mem);

      } else if (left_int && right_float) {
        this->as.write_cvtsi2sd(this->float_target_register, left_mem);
        this->as.write_mulsd(this->float_target_register, right_mem);

      } else if (left_float && right_int) {
        this->as.write_cvtsi2sd(this->float_target_register, right_mem);
        this->as.write_mulsd(this->float_target_register, left_mem);

        // watch it: in this case the type is different from right_type
        this->current_type = Variable(ValueType::Float);

      } else if (left_float && right_float) {
        this->as.write_mulsd(this->float_target_register, left_mem);

      } else {
        throw compile_error("Multiplication not implemented for " + left_type.str() + " and " + right_type.str(), this->file_offset);
      }
      break;

    case BinaryOperator::Division: {
      // we'll need a temp reg for these
      Register tmp_xmm = this->available_register_except(
          {this->float_target_register}, true);
      MemoryReference tmp_xmm_mem(tmp_xmm);

      // TODO: check if right is zero and raise ZeroDivisionError if so

      if (left_int && right_int) {
        this->as.write_cvtsi2sd(this->float_target_register, left_mem);
        this->as.write_cvtsi2sd(tmp_xmm, right_mem);
        this->as.write_divsd(this->float_target_register, tmp_xmm_mem);

      } else if (left_int && right_float) {
        this->as.write_cvtsi2sd(this->float_target_register, left_mem);
        this->as.write_divsd(this->float_target_register, right_mem);

      } else if (left_float && right_int) {
        this->as.write_movsd(float_target_mem, left_mem);
        this->as.write_cvtsi2sd(tmp_xmm, right_mem);
        this->as.write_divsd(this->float_target_register, tmp_xmm_mem);

      } else if (left_float && right_float) {
        this->as.write_movsd(float_target_mem, left_mem);
        this->as.write_divsd(this->float_target_register, right_mem);

      } else {
        throw compile_error("Division not implemented for " + left_type.str() + " and " + right_type.str(), this->file_offset);
      }

      this->current_type = Variable(ValueType::Float);
      break;
    }

    case BinaryOperator::Modulus:
      if (left_bytes || left_unicode) {
        // AnalysisVisitor should have already done the typechecking - all we
        // have to do is call the right format function
        if (right_tuple) {
          const void* fn = left_bytes ?
              void_fn_ptr(&bytes_format) : void_fn_ptr(&unicode_format);
          this->write_function_call(common_object_reference(fn),
              {left_mem, right_mem, r14}, {}, -1, this->target_register);

        } else {
          // in this case (unlike above) right might not be an object, so we
          // have to tell the callee whether it is or not
          Register r = available_register(Register::RDX);
          MemoryReference r_mem(r);
          if (!right_holding_reference) {
            this->as.write_xor(r_mem, r_mem);
          } else {
            this->as.write_mov(r_mem, 1);
          }

          const void* fn = left_bytes ?
              void_fn_ptr(&bytes_format_one) : void_fn_ptr(&unicode_format_one);
          this->write_function_call(common_object_reference(fn),
              {left_mem, right_mem, r_mem, r14}, {}, -1, this->target_register);
        }

        // if returned a new reference to a string of some sort
        this->current_type = Variable(left_bytes ?
            ValueType::Bytes : ValueType::Unicode);
        this->holding_reference = true;

        // we pass the references we're holding directly into the function, and
        // it will delete them for us, so we don't need to do it ourselves later
        left_holding_reference = false;
        right_holding_reference = false;
        break;
      }

    case BinaryOperator::IntegerDivision: {
      bool is_mod = (a->oper == BinaryOperator::Modulus);

      // we only support numeric arguments here
      if (!left_numeric || !right_numeric) {
        throw compile_error("integer division and modulus not implemented for "
            + left_type.str() + " and " + right_type.str(), this->file_offset);
      }

      // if both arguments are ints, do integer div/mod
      if (left_int && right_int) {
        // x86 has a reasonable imul opcode, but no reasonable idiv; we have to
        // use rdx and rax
        bool push_rax = (this->target_register != Register::RAX) &&
            !this->register_is_available(Register::RAX);
        bool push_rdx = (this->target_register != Register::RDX) &&
            !this->register_is_available(Register::RDX);
        if (push_rax) {
          this->write_push(Register::RAX);
        }
        if (push_rdx) {
          this->write_push(Register::RDX);
        }

        // TODO: check if right is zero and raise ZeroDivisionError if so

        this->as.write_mov(rax, left_mem);
        this->as.write_xor(rdx, rdx);
        this->as.write_idiv(right_mem);
        if (is_mod) {
          if (this->target_register != Register::RDX) {
            this->as.write_mov(target_mem, rdx);
          }
        } else {
          if (this->target_register != Register::RAX) {
            this->as.write_mov(target_mem, rax);
          }
        }

        if (push_rdx) {
          this->write_pop(Register::RDX);
        }
        if (push_rax) {
          this->write_pop(Register::RAX);
        }

        // Int // Int == Int
        this->current_type = Variable(ValueType::Int);

      } else {
        Register left_xmm = this->float_target_register;
        Register right_xmm = this->available_register_except({left_xmm}, true);
        MemoryReference left_xmm_mem(left_xmm);
        MemoryReference right_xmm_mem(right_xmm);

        if (left_float) {
          this->as.write_movsd(left_xmm_mem, left_mem);
        } else {
          this->as.write_cvtsi2sd(left_xmm, left_mem);
        }
        if (right_float) {
          this->as.write_movsd(right_xmm_mem, right_mem);
        } else {
          this->as.write_cvtsi2sd(right_xmm, right_mem);
        }

        // TODO: check if right is zero and raise ZeroDivisionError if so

        if (is_mod) {
          // TODO: can we do this without using a third xmm register?
          Register tmp_xmm = this->available_register_except(
              {left_xmm, right_xmm}, true);
          MemoryReference tmp_xmm_mem(tmp_xmm);
          this->as.write_movsd(tmp_xmm_mem, left_xmm_mem);
          this->as.write_divsd(tmp_xmm, right_xmm_mem);
          this->as.write_roundsd(tmp_xmm, tmp_xmm_mem, 3);
          this->as.write_mulsd(tmp_xmm, right_xmm_mem);
          this->as.write_subsd(left_xmm, tmp_xmm_mem);
        } else {
          this->as.write_divsd(left_xmm, right_xmm_mem);
          this->as.write_roundsd(left_xmm, left_xmm_mem, 3);
        }

        // Float // Int == Int // Float == Float // Float == Float
        this->current_type = Variable(ValueType::Float);
      }
      break;
    }

    case BinaryOperator::Exponentiation:
      if (left_int && right_int) {
        // if the exponent is negative, throw ValueError. unfortunately we can't
        // fix this in a consistent way since the return type of the expression
        // depends on the value, not on any part of the code that we can
        // analyze. as far as I can tell this is the only case in the entire
        // language that has this property, and it's easy to work around (just
        // change the source to do `1/(a**b)` instead), so we'll make the user
        // do that instead

        string positive_label = string_printf("__BinaryOperation_%p_pow_not_neg", a);
        this->as.write_label(string_printf("__BinaryOperation_%p_pow_check_neg", a));
        this->as.write_cmp(right_mem, 0);
        this->as.write_jge(positive_label);
        this->write_raise_exception(ValueError_class_id);
        this->as.write_label(positive_label);

        // implementation mirrors notes/pow.s except that we load the base value
        // into a temp register
        string again_label = string_printf("__BinaryOperation_%p_pow_again", a);
        string skip_base_label = string_printf("__BinaryOperation_%p_pow_skip_base", a);
        this->as.write_mov(target_mem, 1);
        this->as.write_mov(temp_mem, left_mem);
        this->as.write_label(again_label);
        this->as.write_test(right_mem, 1);
        this->as.write_jz(skip_base_label);
        this->as.write_imul(target_mem.base_register, temp_mem);
        this->as.write_label(skip_base_label);
        this->as.write_imul(temp_mem.base_register, temp_mem);
        this->as.write_shr(right_mem, 1);
        this->as.write_jnz(again_label);
        break;

      } else if (left_float || right_float) {
        Register left_xmm = this->available_register(Register::None, true);
        Register right_xmm = this->available_register_except({left_xmm}, true);
        MemoryReference left_xmm_mem(left_xmm);
        MemoryReference right_xmm_mem(right_xmm);

        if (!left_float) { // left is Int, right is Float
          this->as.write_cvtsi2sd(left_xmm, left_mem);
        } else {
          this->as.write_movsd(left_xmm_mem, left_mem);
        }

        if (!right_float) { // left is Float, right is Int
          this->as.write_cvtsi2sd(right_xmm, right_mem);
          // watch it: in this case the type is different from right_type
          this->current_type = Variable(ValueType::Float);

        } else {
          this->as.write_movsd(right_xmm_mem, right_mem);
        }

        static const void* pow_fn = void_fn_ptr(static_cast<double(*)(double, double)>(&pow));
        this->write_function_call(common_object_reference(pow_fn), {},
            {left_xmm_mem, right_xmm_mem}, -1, this->float_target_register, true);
        break;
      }

      // TODO
      throw compile_error("Exponentiation not implemented for " + left_type.str() + " and " + right_type.str(), this->file_offset);

    default:
      throw compile_error("unhandled binary operator", this->file_offset);
  }

  this->as.write_label(string_printf("__BinaryOperation_%p_cleanup", a));

  // if either value requires destruction, do so now
  if (left_holding_reference || right_holding_reference) {
    // save the return value before destroying the temp values
    this->write_push(this->target_register);

    // destroy the temp values
    if (type_has_refcount(left_type.type)) {
      this->as.write_label(string_printf("__BinaryOperation_%p_destroy_left", a));
      this->write_delete_reference(MemoryReference(Register::RSP, 8), left_type.type);
    }
    if (type_has_refcount(right_type.type)) {
      this->as.write_label(string_printf("__BinaryOperation_%p_destroy_right", a));
      this->write_delete_reference(MemoryReference(Register::RSP, 16), right_type.type);
    }

    // load the result again and clean up the stack
    this->as.write_mov(MemoryReference(this->target_register),
        MemoryReference(Register::RSP, 0));
    this->adjust_stack(0x18);

  // no destructor calls necessary; just remove left and right from the stack
  } else {
    this->adjust_stack(0x10);
  }

  this->as.write_label(string_printf("__BinaryOperation_%p_complete", a));
}

void CompilationVisitor::visit(TernaryOperation* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  if (a->oper != TernaryOperator::IfElse) {
    throw compile_error("unrecognized ternary operator", this->file_offset);
  }

  this->as.write_label(string_printf("__TernaryOperation_%p_evaluate", a));

  // generate the condition evaluation
  a->center->accept(this);

  // if the value is always truthy or always falsey, don't bother generating the
  // unused side
  if (this->is_always_truthy(this->current_type)) {
    this->write_delete_held_reference(MemoryReference(this->target_register));
    a->left->accept(this);
    return;
  }
  if (this->is_always_falsey(this->current_type)) {
    this->write_delete_held_reference(MemoryReference(this->target_register));
    a->right->accept(this);
    return;
  }

  // left comes first in the code
  string false_label = string_printf("TernaryOperation_%p_condition_false", a);
  string end_label = string_printf("TernaryOperation_%p_end", a);
  this->write_current_truth_value_test();
  this->as.write_jz(false_label); // skip left

  // generate code for the left (True) value
  this->write_delete_held_reference(MemoryReference(this->target_register));
  a->left->accept(this);
  this->as.write_jmp(end_label);
  Variable left_type = move(this->current_type);

  // generate code for the right (False) value
  this->as.write_label(false_label);
  this->write_delete_held_reference(MemoryReference(this->target_register));
  a->right->accept(this);
  this->as.write_label(end_label);

  // TODO: support different value types (maybe by splitting the function)
  if (left_type != this->current_type) {
    throw compile_error("sides have different types", this->file_offset);
  }
}

void CompilationVisitor::visit(ListConstructor* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  this->as.write_label(string_printf("__ListConstructor_%p_setup", a));

  // we'll use rbx to store the list ptr while constructing items and I'm lazy
  if (this->target_register == Register::RBX) {
    throw compile_error("cannot use rbx as target register for list construction");
  }
  this->write_push(Register::RBX);
  int64_t previously_reserved_registers = this->write_push_reserved_registers();

  // allocate the list object
  this->as.write_label(string_printf("__ListConstructor_%p_allocate", a));
  vector<const MemoryReference> int_args({
    MemoryReference(int_argument_register_order[0]),
    MemoryReference(int_argument_register_order[1]),
    MemoryReference(int_argument_register_order[2]),
    r14,
  });
  this->as.write_xor(int_args[0], int_args[0]);
  this->as.write_mov(int_args[1], a->items.size());
  this->as.write_xor(int_args[2], int_args[2]); // we'll set items_are_objects later
  this->write_function_call(common_object_reference(void_fn_ptr(&list_new)),
      int_args, {}, -1, this->target_register);

  // save the list items pointer
  this->as.write_mov(rbx, MemoryReference(this->target_register, 0x28));
  this->write_push(Register::RAX);

  // generate code for each item and track the extension type
  Variable extension_type;
  size_t item_index = 0;
  for (const auto& item : a->items) {
    this->as.write_label(string_printf("__ListConstructor_%p_item_%zu", a, item_index));
    item->accept(this);
    if (this->current_type.type == ValueType::Float) {
      this->as.write_movsd(MemoryReference(Register::RBX, item_index * 8),
          MemoryReference(this->float_target_register));
    } else {
      this->as.write_mov(MemoryReference(Register::RBX, item_index * 8),
          MemoryReference(this->target_register));
    }
    item_index++;

    // merge the type with the known extension type
    if (extension_type.type == ValueType::Indeterminate) {
      extension_type = move(this->current_type);
      extension_type.clear_value();
    } else if (!extension_type.types_equal(this->current_type)) {
      throw compile_error("list contains different object types: " +
          extension_type.type_only().str() + " and " + this->current_type.type_only().str());
    }
  }

  // get the list pointer back
  this->as.write_label(string_printf("__ListConstructor_%p_finalize", a));
  this->write_pop(this->target_register);

  // if the extension type is an object of some sort, set the destructor flag
  if (type_has_refcount(extension_type.type)) {
    this->as.write_mov(MemoryReference(this->target_register, 0x20), 1);
  }

  // restore the regs we saved
  this->write_pop_reserved_registers(previously_reserved_registers);
  this->write_pop(Register::RBX);

  // the result type is a new reference to a List[extension_type]
  vector<Variable> extension_types({extension_type});
  this->current_type = Variable(ValueType::List, extension_types);
  this->holding_reference = true;
}

void CompilationVisitor::visit(SetConstructor* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  // TODO: generate malloc call, item constructor and insert code
  throw compile_error("SetConstructor not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(DictConstructor* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  // TODO: generate malloc call, item constructor and insert code
  throw compile_error("DictConstructor not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(TupleConstructor* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  this->as.write_label(string_printf("__TupleConstructor_%p_setup", a));

  // we'll use rbx to store the tuple ptr while constructing items and I'm lazy
  if (this->target_register == Register::RBX) {
    throw compile_error("cannot use rbx as target register for tuple construction");
  }
  this->write_push(Register::RBX);
  int64_t previously_reserved_registers = this->write_push_reserved_registers();

  // allocate the tuple object
  this->as.write_label(string_printf("__TupleConstructor_%p_allocate", a));
  vector<const MemoryReference> int_args({
    MemoryReference(int_argument_register_order[0]),
    MemoryReference(int_argument_register_order[1]),
    r14,
  });
  this->as.write_xor(int_args[0], int_args[0]);
  this->as.write_mov(int_args[1], a->items.size());
  this->write_function_call(common_object_reference(void_fn_ptr(&tuple_new)),
      int_args, {}, -1, this->target_register);

  // save the tuple items pointer
  this->as.write_lea(Register::RBX, MemoryReference(this->target_register, 0x18));
  this->write_push(Register::RAX);

  // generate code for each item and track the extension type
  vector<Variable> extension_types;
  for (const auto& item : a->items) {
    this->as.write_label(string_printf("__TupleConstructor_%p_item_%zu", a, extension_types.size()));
    item->accept(this);
    if (this->current_type.type == ValueType::Float) {
      this->as.write_movsd(MemoryReference(Register::RBX, extension_types.size() * 8),
          MemoryReference(this->float_target_register));
    } else {
      this->as.write_mov(MemoryReference(Register::RBX, extension_types.size() * 8),
          MemoryReference(this->target_register));
    }
    extension_types.emplace_back(move(this->current_type));
  }

  // generate code to write the has_refcount map
  // TODO: we can coalesce these writes for tuples larger than 8 items
  this->as.write_label(string_printf("__TupleConstructor_%p_has_refcount_map", a));
  size_t types_handled = 0;
  while (types_handled < extension_types.size()) {
    uint8_t value = 0;
    for (size_t x = 0; (x < 8) && ((types_handled + x) < extension_types.size()); x++) {
      if (type_has_refcount(extension_types[types_handled + x].type)) {
        value |= (0x80 >> x);
      }
    }
    this->as.write_mov(MemoryReference(Register::RBX,
        extension_types.size() * 8 + (types_handled / 8)), value, OperandSize::Byte);
    types_handled += 8;
  }

  // get the tuple pointer back
  this->as.write_label(string_printf("__TupleConstructor_%p_finalize", a));
  this->write_pop(this->target_register);

  // restore the regs we saved
  this->write_pop_reserved_registers(previously_reserved_registers);
  this->write_pop(Register::RBX);

  // the result type is a new reference to a Tuple[extension_types]
  this->current_type = Variable(ValueType::Tuple, extension_types);
  this->holding_reference = true;
}

void CompilationVisitor::visit(ListComprehension* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  // TODO
  throw compile_error("ListComprehension not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(SetComprehension* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  // TODO
  throw compile_error("SetComprehension not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(DictComprehension* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  // TODO
  throw compile_error("DictComprehension not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(LambdaDefinition* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

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

struct FunctionCallArgumentValue {
  string name;
  shared_ptr<Expression> passed_value;
  Variable default_value; // Indeterminate for positional args
  Variable type;

  Register target_register; // if the type is Float, this is an xmm register
  ssize_t stack_offset; // if < 0, this argument isn't stored on the stack

  bool is_exception_block;
  bool evaluate_instance_pointer;

  FunctionCallArgumentValue(const std::string& name) : name(name),
      passed_value(NULL), default_value(), type(), stack_offset(0),
      is_exception_block(false), evaluate_instance_pointer(false) { }
};

void CompilationVisitor::visit(FunctionCall* a) {
  this->file_offset = a->file_offset;

  // TODO: we should support dynamic function references (e.g. looking them up
  // in dicts and whatnot)
  if (a->callee_function_id == 0) {
    throw compile_error("can\'t resolve function reference", this->file_offset);
  }

  // if the target register is reserved, its value will be preserved instead of
  // being overwritten by the function's return value, which probably isn't what
  // we want
  if (!this->register_is_available(this->target_register)) {
    throw compile_error("target register is reserved at function call time",
        this->file_offset);
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
  vector<FunctionCallArgumentValue> arg_values;

  // for class member functions, the first argument is automatically populated
  // and is the instance object
  if (fn->class_id) {
    arg_values.emplace_back(fn->args[0].name);
    auto& arg = arg_values.back();

    // if the function being called is __init__, allocate a class object first
    // and push it as the first argument (passing an Instance with a NULL
    // pointer instructs the later code to allocate an instance)
    if (fn->is_class_init()) {
      arg.default_value = Variable(ValueType::Instance, fn->id, nullptr);

    // if it's not __init__, we'll have to know what the instance is; instruct
    // the later code to evaluate the instance pointer instead of the function
    // object
    } else {
      arg.passed_value = a->function;
      arg.evaluate_instance_pointer = true;
    }
    arg.type = Variable(ValueType::Instance, fn->class_id, nullptr);
  }

  // push positional args first
  size_t call_arg_index = 0;
  size_t callee_arg_index = (fn->class_id != 0);
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

    arg_values.emplace_back(callee_arg.name);
    arg_values.back().passed_value = call_arg;
    arg_values.back().default_value = Variable(ValueType::Indeterminate);
  }

  // push remaining args, in the order the function defines them
  for (; callee_arg_index < fn->args.size(); callee_arg_index++) {
    const auto& callee_arg = fn->args[callee_arg_index];

    arg_values.emplace_back(callee_arg.name);
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
    throw compile_error(string_printf(
        "incorrect argument count in function call (given: %zu, expected: %zu)",
        arg_values.size(), fn->args.size()), this->file_offset);
  }

  // if the function takes the exception block as an argument, push it here
  if (fn->pass_exception_block) {
    arg_values.emplace_back("(exception block)");
    arg_values.back().is_exception_block = true;
  }

  // push all reserved registers
  int64_t previously_reserved_registers = this->write_push_reserved_registers();

  // reserve enough stack space for the worst case - all args are ints (since
  // there are fewer int registers available)
  ssize_t arg_stack_bytes = this->write_function_call_stack_prep(arg_values.size());

  // generate code for the arguments
  Register original_target_register = this->target_register;
  Register original_float_target_register = this->float_target_register;
  size_t int_registers_used = 0, float_registers_used = 0, stack_offset = 0;
  for (size_t arg_index = 0; arg_index < arg_values.size(); arg_index++) {
    auto& arg = arg_values[arg_index];

    // if it's an int, it goes into the next int register; if it's a float, it
    // goes into the next float register. if either of these are exhausted, it
    // goes onto the stack
    if (int_registers_used == int_argument_register_order.size()) {
      this->target_register = this->available_register();
    } else {
      this->target_register = int_argument_register_order[int_registers_used];
    }
    if (float_registers_used == float_argument_register_order.size()) {
      // TODO: none of the registers will be available when this happens; figure
      // out a way to deal with this
      this->float_target_register = this->available_register(Register::None, true);
    } else {
      this->float_target_register = float_argument_register_order[float_registers_used];
    }

    // generate code for the argument's value

    // if the argument has a passed value, generate code to compute it
    if (arg.passed_value.get()) {
      // if this argument is `self`, then it actually comes from the function
      // expression (and we evaluate the instance pointer there instead)
      if (arg.evaluate_instance_pointer) {
        this->as.write_label(string_printf("__FunctionCall_%p_get_instance_pointer", a));
        if (this->evaluating_instance_pointer) {
          throw compile_error("recursive instance pointer evaluation", this->file_offset);
        }
        this->evaluating_instance_pointer = true;
        a->function->accept(this);
        if (this->evaluating_instance_pointer) {
          throw compile_error("instance pointer evaluation failed", this->file_offset);
        }
        if (!type_has_refcount(this->current_type.type)) {
          throw compile_error("instance pointer evaluation resulted in " + this->current_type.str(),
              this->file_offset);
        }
        arg.type = move(this->current_type);

      } else {
        this->as.write_label(string_printf("__FunctionCall_%p_evaluate_arg_%zu_passed_value",
            a, arg_index));
        arg.passed_value->accept(this);
        arg.type = move(this->current_type);
      }

    // if the argument is the instance object, figure out what it is
    } else if (fn->is_class_init() && (arg_index == 0)) {
      if (arg.default_value.type != ValueType::Instance) {
        throw compile_error("first argument to class constructor is not an instance", this->file_offset);
      }

      auto* cls = this->global->context_for_class(fn->id);
      if (!cls) {
        throw compile_error("__init__ call does not have an associated class");
      }

      this->as.write_label(string_printf("__FunctionCall_%p_evaluate_arg_%zu_alloc_instance",
          a, arg_index));
      this->write_alloc_class_instance(cls->id);

      arg.type = arg.default_value;
      this->current_type = Variable(ValueType::Instance, cls->id, NULL);
      this->holding_reference = true;

    // if the argument is the exception block, copy it from r14
    } else if (arg.is_exception_block) {
      this->as.write_label(string_printf("__FunctionCall_%p_evaluate_arg_%zu_exception_block",
          a, arg_index));
      this->as.write_mov(MemoryReference(this->target_register), r14);

    } else {
      this->as.write_label(string_printf("__FunctionCall_%p_evaluate_arg_%zu_default_value",
          a, arg_index));
      if (!arg.default_value.value_known) {
        throw compile_error(string_printf(
          "required function argument %zu (%s) does not have a value",
          arg_index, arg.name.c_str()), this->file_offset);
      }
      this->write_code_for_value(arg.default_value);
      arg.type = arg.default_value;
    }

    // bugcheck: if the value has a refcount, we had better be holding a
    // reference to it
    if (type_has_refcount(arg.type.type) && !this->holding_reference) {
      throw compile_error(string_printf(
          "function call argument %zu is a non-held reference", arg_index),
          this->file_offset);
    }

    // store the value on the stack if needed; otherwise, mark the register as
    // reserved
    if (arg.type.type == ValueType::Float) {
      if (float_registers_used != float_argument_register_order.size()) {
        this->reserve_register(this->float_target_register, true);
        float_registers_used++;
      } else {
        this->as.write_movsd(MemoryReference(Register::RSP, stack_offset),
            MemoryReference(this->float_target_register));
        stack_offset += sizeof(double);
      }
    } else {
      if (int_registers_used != int_argument_register_order.size()) {
        this->reserve_register(this->target_register);
        int_registers_used++;
      } else {
        this->as.write_mov(MemoryReference(Register::RSP, stack_offset),
            MemoryReference(this->target_register));
        stack_offset += sizeof(int64_t);
      }
    }
  }
  this->target_register = original_target_register;
  this->float_target_register = original_float_target_register;

  // we can now unreserve the argument registers. there should be no reserved
  // registers at this point because we already saved them above
  this->release_all_registers(false);
  this->release_all_registers(true);

  // figure out which fragment to call
  vector<Variable> arg_types;
  unordered_map<string, Variable> callee_local_overrides;
  for (const auto& arg : arg_values) {
    if (arg.is_exception_block) {
      continue; // don't include exc_block in the type signature
    }
    arg_types.emplace_back(arg.type);
    callee_local_overrides.emplace(arg.name, arg.type);
  }

  // get the argument type signature
  string arg_signature;
  try {
    arg_signature = type_signature_for_variables(arg_types);
  } catch (const invalid_argument& e) {
    throw compile_error(string_printf(
        "can\'t generate argument signature for function call: %s", e.what()),
        this->file_offset);
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

  // call the fragment. note that the stack is already properly aligned here
  this->as.write_mov(Register::RAX, reinterpret_cast<int64_t>(fragment->compiled));
  this->as.write_call(rax);

  // if the function raised an exception, the return value is meaningless;
  // instead we should continue unwinding the stack
  string no_exc_label = string_printf("__FunctionCall_%p_no_exception", a);
  this->as.write_test(r15, r15);
  this->as.write_jz(no_exc_label);
  this->as.write_jmp(common_object_reference(void_fn_ptr(&_unwind_exception_internal)));
  this->as.write_label(no_exc_label);

  // put the return value into the target register
  if (fragment->return_type.type == ValueType::Float) {
    if (this->target_register != Register::RAX) {
      this->as.write_label(string_printf("__FunctionCall_%p_save_return_value", a));
      this->as.write_movsd(MemoryReference(this->float_target_register), xmm0);
    }
  } else {
    if (this->target_register != Register::RAX) {
      this->as.write_label(string_printf("__FunctionCall_%p_save_return_value", a));
      this->as.write_mov(MemoryReference(this->target_register), rax);
    }
  }

  // functions always return new references, unless they return trivial types
  this->current_type = fragment->return_type;
  this->holding_reference = type_has_refcount(this->current_type.type);

  // note: we don't have to destroy the function arguments; we passed the
  // references that we generated directly into the function and it's
  // responsible for deleting those references

  // unreserve the argument stack space
  this->as.write_label(string_printf("__FunctionCall_%p_restore_stack", a));
  if (arg_stack_bytes) {
    this->adjust_stack(arg_stack_bytes);
  }

  // pop the registers that we saved earlier
  this->write_pop_reserved_registers(previously_reserved_registers);
}

void CompilationVisitor::visit(ArrayIndex* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  // get the collection
  a->array->accept(this);
  Variable collection_type = move(this->current_type);

  if ((collection_type.type != ValueType::List) && (collection_type.type != ValueType::Tuple)) {
    // TODO
    throw compile_error("ArrayIndex not yet implemented for collections of type " + collection_type.str(),
        this->file_offset);
  }

  // I'm lazy and don't want to duplicate work - just call the appropriate
  // get_item function

  Register original_target_register = this->target_register;
  int64_t previously_reserved_registers = this->write_push_reserved_registers();

  // arg 1 is the list/tuple object
  if (this->target_register != Register::RDI) {
    this->as.write_mov(rdi, MemoryReference(this->target_register));
  }

  // for lists, the index can be dynamic; compute it now
  int64_t tuple_index = a->index_value;
  if (collection_type.type == ValueType::List) {
    this->target_register = Register::RSI;
    this->reserve_register(Register::RDI);
    a->index->accept(this);
    if (this->current_type.type != ValueType::Int) {
      throw compile_error("list index must be Int; here it\'s " + this->current_type.str(),
          this->file_offset);
    }
    this->release_register(Register::RDI);

  // for tuples, the index must be static since the result type depends on it.
  // for this reason, it also needs to be in range of the extension types
  } else {
    if (!a->index_constant) {
      throw compile_error("tuple indexes must be constants", this->file_offset);
    }
    if (tuple_index < 0) {
      tuple_index += collection_type.extension_types.size();
    }
    if ((tuple_index < 0) || (tuple_index >= collection_type.extension_types.size())) {
      throw compile_error("tuple index out of range", this->file_offset);
    }
    this->as.write_mov(Register::RSI, tuple_index);
  }

  // now call the function
  const void* fn = (collection_type.type == ValueType::List) ?
      void_fn_ptr(&list_get_item) : void_fn_ptr(&tuple_get_item);
  this->write_function_call(common_object_reference(fn), {rdi, rsi, r14}, {},
      -1, original_target_register);

  // for lists, the return type is the extension type
  if (collection_type.type == ValueType::List) {
    this->current_type = collection_type.extension_types[0];
  // for tuples, the return type is one of the extension types, determined by
  // the static index value
  } else {
    this->current_type = collection_type.extension_types[tuple_index];
  }

  // if the return value is a float, it's currently in an int register; move it
  // to an xmm reg if needed
  if (this->current_type.type == ValueType::Float) {
    this->as.write_movq_to_xmm(this->float_target_register,
        MemoryReference(this->target_register));
  }

  // restore state
  this->write_pop_reserved_registers(previously_reserved_registers);
  this->target_register = original_target_register;
}

void CompilationVisitor::visit(ArraySlice* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  // TODO
  throw compile_error("ArraySlice not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(IntegerConstant* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  this->as.write_mov(this->target_register, a->value);
  this->current_type = Variable(ValueType::Int);
  this->holding_reference = false;
}

void CompilationVisitor::visit(FloatConstant* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  this->write_load_double(this->float_target_register, a->value);
  this->current_type = Variable(ValueType::Float);
  this->holding_reference = false;
}

void CompilationVisitor::visit(BytesConstant* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  const BytesObject* o = this->global->get_or_create_constant(a->value);
  this->as.write_mov(this->target_register, reinterpret_cast<int64_t>(o));
  this->write_add_reference(this->target_register);

  this->current_type = Variable(ValueType::Bytes);
  this->holding_reference = true;
}

void CompilationVisitor::visit(UnicodeConstant* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  const UnicodeObject* o = this->global->get_or_create_constant(a->value);
  this->as.write_mov(this->target_register, reinterpret_cast<int64_t>(o));
  this->write_add_reference(this->target_register);

  this->current_type = Variable(ValueType::Unicode);
  this->holding_reference = true;
}

void CompilationVisitor::visit(TrueConstant* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  this->as.write_mov(this->target_register, 1);
  this->current_type = Variable(ValueType::Bool);
  this->holding_reference = false;
}

void CompilationVisitor::visit(FalseConstant* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  MemoryReference target_mem(this->target_register);
  this->as.write_xor(target_mem, target_mem);
  this->current_type = Variable(ValueType::Bool);
  this->holding_reference = false;
}

void CompilationVisitor::visit(NoneConstant* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  MemoryReference target_mem(this->target_register);
  this->as.write_xor(target_mem, target_mem);
  this->current_type = Variable(ValueType::None);
  this->holding_reference = false;
}

void CompilationVisitor::visit(VariableLookup* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  VariableLocation loc = this->location_for_variable(a->name);
  bool has_refcount = type_has_refcount(loc.type.type);

  // if this is an object, add a reference to it; otherwise just load it
  if (loc.type.type == ValueType::Float) {
    this->as.write_movq_to_xmm(this->float_target_register, loc.mem);
  } else {
    this->as.write_mov(MemoryReference(this->target_register), loc.mem);
    if (has_refcount) {
      this->write_add_reference(this->target_register);
    }
  }

  this->current_type = loc.type;
  if (this->current_type.type == ValueType::Indeterminate) {
    throw compile_error("variable has Indeterminate type: " + loc.str(), this->file_offset);
  }
  this->holding_reference = has_refcount;
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
    // so we require Imported phase here to enforce this ordering later.
    auto module = this->global->get_module_at_phase(a->base_module_name,
        ModuleAnalysis::Phase::Imported);
    VariableLocation loc = this->location_for_global(module.get(), a->name);
    bool has_refcount = type_has_refcount(loc.type.type);

    // if this is an object, add a reference to it; otherwise just load it
    this->as.write_mov(MemoryReference(this->target_register), loc.mem);
    if (has_refcount) {
      this->write_add_reference(this->target_register);
    }

    this->current_type = loc.type;
    if (this->current_type.type == ValueType::Indeterminate) {
      throw compile_error("attribute has Indeterminate type", this->file_offset);
    }
    this->holding_reference = has_refcount;

    return;
  }

  // there had better be a base
  if (!a->base.get()) {
    throw compile_error("attribute lookup has no base", this->file_offset);
  }

  if (this->evaluating_instance_pointer) {
    this->evaluating_instance_pointer = false;

    this->as.write_label(string_printf("__AttributeLookup_%p_evaluate_instance", a));
    a->base->accept(this);
    if (!this->holding_reference) {
      throw compile_error("instance pointer evaluation resulted in non-held reference",
          this->file_offset);
    }
    return;
  }

  // evaluate the base object into another register
  Register attr_register = this->target_register;
  Register base_register = this->available_register_except({attr_register});
  this->target_register = base_register;
  this->as.write_label(string_printf("__AttributeLookup_%p_evaluate_base", a));
  a->base->accept(this);

  // if the base object is a class, write code that gets the attribute
  if (this->current_type.type == ValueType::Instance) {
    auto* cls = this->global->context_for_class(this->current_type.class_id);
    VariableLocation loc = this->location_for_attribute(cls, a->name, this->target_register);

    // get the attribute value
    this->as.write_label(string_printf("__AttributeLookup_%p_get_value", a));
    this->as.write_mov(MemoryReference(attr_register), loc.mem);
    bool attr_has_refcount = type_has_refcount(loc.type.type);
    if (attr_has_refcount) {
      this->reserve_register(base_register);
      this->write_add_reference(attr_register);
      this->release_register(base_register);
    }

    // if we're holding a reference to the base, delete that reference now
    if (this->holding_reference) {
      this->reserve_register(attr_register);
      this->write_delete_held_reference(MemoryReference(base_register));
      this->release_register(attr_register);
    }

    this->target_register = attr_register;
    this->current_type = loc.type;
    this->holding_reference = attr_has_refcount;
    return;
  }

  // TODO
  throw compile_error("AttributeLookup not yet implemented on " + this->current_type.str(),
      this->file_offset);
}

void CompilationVisitor::visit(TupleLValueReference* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  // TODO
  throw compile_error("TupleLValueReference not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(ArrayIndexLValueReference* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  // TODO
  throw compile_error("ArrayIndexLValueReference not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(ArraySliceLValueReference* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  // TODO
  throw compile_error("ArraySliceLValueReference not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(AttributeLValueReference* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  // if it's an object attribute, store the value and evaluate the object
  if (a->base.get()) {

    // we had better be holding a reference to the value
    if (type_has_refcount(this->current_type.type) && !this->holding_reference) {
      throw compile_error("assignment of non-held reference to attribute",
          this->file_offset);
    }

    // don't touch my value plz
    Register value_register = this->target_register;
    this->reserve_register(this->target_register);
    this->target_register = this->available_register();
    Variable value_type = move(this->current_type);

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
    const auto& attr = cls->attributes.at(a->name);
    if (!attr.types_equal(loc.type)) {
      string attr_type = attr.str();
      string new_type = value_type.str();
      throw compile_error(string_printf("attribute %s changes type from %s to %s",
          a->name.c_str(), attr_type.c_str(), new_type.c_str()),
          this->file_offset);
    }

    // if the attribute type has a refcount, delete the old value
    this->reserve_register(this->target_register);
    if (type_has_refcount(loc.type.type)) {
      this->write_delete_reference(loc.mem, loc.type.type);
    }
    this->release_register(this->target_register);

    // write the value into the right attribute
    if (value_type.type == ValueType::Float) {
      this->as.write_movsd(loc.mem, MemoryReference(this->float_target_register));
    } else {
      this->as.write_mov(loc.mem, MemoryReference(value_register));
    }

    // if we're holding a reference to the base, delete it
    this->write_delete_held_reference(MemoryReference(this->target_register));

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
    } else if (!target_variable->types_equal(loc.type)) {
      string target_type_str = target_variable->str();
      string value_type_str = current_type.str();
      throw compile_error(string_printf("variable %s changes type from %s to %s\n",
          loc.name.c_str(), target_type_str.c_str(),
          value_type_str.c_str()), this->file_offset);
    }

    // if the variable has a refcount, delete the old value
    this->reserve_register(this->target_register);
    if (type_has_refcount(loc.type.type)) {
      this->write_delete_reference(loc.mem, loc.type.type);
    }
    this->release_register(this->target_register);

    // save the value to the appropriate slot
    if (target_variable->type == ValueType::Float) {
      this->as.write_movsd(loc.mem, MemoryReference(this->float_target_register));
    } else {
      this->as.write_mov(loc.mem, MemoryReference(this->target_register));
    }
  }
}

// statement visitation
void CompilationVisitor::visit(ModuleStatement* a) {
  this->file_offset = a->file_offset;

  this->as.write_label(string_printf("__ModuleStatement_%p", a));

  this->stack_bytes_used = 8;
  this->holding_reference = false;

  // this is essentially a function, but it will only be called once. we still
  // have to treat it like a function, but it has no local variables (everything
  // it writes is global, so based on R13, not RSP). the global pointer is
  // passed as an argument (RDI) instead of already being in R13, so we move it
  // into place. it returns the active exception object (NULL means success).
  this->write_push(Register::RBP);
  this->as.write_mov(rbp, rsp);
  this->write_push(Register::R12);
  this->as.write_mov(Register::R12, reinterpret_cast<int64_t>(common_object_base()));
  this->write_push(Register::R13);
  this->as.write_mov(Register::R13, reinterpret_cast<int64_t>(this->global->global_space));
  this->write_push(Register::R14);
  this->as.write_xor(r14, r14);
  this->write_push(Register::R15);
  this->as.write_xor(r15, r15);

  // create an exception block for the root scope
  // this exception block just returns from the module scope - but the calling
  // code checks for a nonzero return value (which means an exception is active)
  // and will handle it appropriately
  string exc_label = string_printf("__ModuleStatement_%p_exc", a);
  this->as.write_label(string_printf("__ModuleStatement_%p_create_exc_block", a));
  this->write_create_exception_block({}, exc_label);

  // generate the function's code
  this->target_register = Register::RAX;
  this->as.write_label(string_printf("__ModuleStatement_%p_body", a));
  this->RecursiveASTVisitor::visit(a);

  // hooray we're done
  this->as.write_label(string_printf("__ModuleStatement_%p_return", a));
  this->adjust_stack(return_exception_block_size);
  this->as.write_mov(rax, r15);
  this->as.write_label(exc_label);
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

  // just evaluate it into some random register and ignore the result
  this->target_register = this->available_register();
  a->expr->accept(this);
  this->write_delete_held_reference(MemoryReference(this->target_register));
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
  if (type_has_refcount(this->current_type.type) && !this->holding_reference) {
    throw compile_error("can\'t assign borrowed reference to " + this->current_type.str(),
        this->file_offset);
  }

  // write the value into the appropriate slot
  this->as.write_label(string_printf("__AssignmentStatement_%p_write_value", a));
  a->target->accept(this);

  // if we were holding a reference, clear the flag - we wrote that reference to
  // memory, but it still exists
  this->holding_reference = false;
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
      this->write_add_reference(this->target_register);
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
  throw compile_error("exec is not supported", this->file_offset);
}

void CompilationVisitor::visit(AssertStatement* a) {
  this->file_offset = a->file_offset;

  string pass_label = string_printf("__AssertStatement_%p_pass", a);

  // evaluate the check expression
  this->as.write_label(string_printf("__AssertStatement_%p_check", a));
  this->target_register = this->available_register();
  a->check->accept(this);

  // check if the result is truthy
  this->as.write_label(string_printf("__AssertStatement_%p_test", a));
  this->write_current_truth_value_test();
  this->as.write_jnz(pass_label);

  // save the result value type so we can destroy it later
  Variable truth_value_type = this->current_type;
  bool was_holding_reference = this->holding_reference;

  // if we get here, then the result was falsey. evaluate the message and save
  // it on the stack so we can move it to the AssertionError object later. (we
  // do this before allocating the AssertionError in case the message evaluation
  // raises an exception)
  this->write_delete_held_reference(MemoryReference(this->target_register));
  if (a->failure_message.get()) {
    this->as.write_label(string_printf("__AssertStatement_%p_evaluate_message", a));
    a->failure_message->accept(this);
  } else {
    this->as.write_label(string_printf("__AssertStatement_%p_generate_message", a));

    // if no message is given, use a blank message
    const UnicodeObject* message = this->global->get_or_create_constant(L"");
    this->as.write_mov(this->target_register, reinterpret_cast<int64_t>(message));
    this->write_add_reference(this->target_register);
  }
  this->write_push(this->target_register);

  // create an AssertionError object. note that we bypass __init__ here
  auto* cls = this->global->context_for_class(AssertionError_class_id);
  if (!cls) {
    throw compile_error("AssertionError class does not exist",
        this->file_offset);
  }

  this->as.write_label(string_printf("__AssertStatement_%p_allocate_instance", a));
  this->write_alloc_class_instance(AssertionError_class_id, false);
  this->as.write_mov(MemoryReference(this->target_register, 24), rax);

  // we should have filled everything in
  if (cls->instance_size() != 32) {
    throw compile_error("did not fill in entire AssertionError structure",
        this->file_offset);
  }

  // now jump to unwind_exception
  this->as.write_label(string_printf("__AssertStatement_%p_unwind", a));
  this->as.write_mov(r15, MemoryReference(this->target_register));
  this->write_pop(Register::RAX);
  this->as.write_jmp(common_object_reference(void_fn_ptr(&_unwind_exception_internal)));

  // if we get here, then the expression was truthy, but we may still need to
  // destroy it if it has a refcount
  this->as.write_label(pass_label);
  this->current_type = truth_value_type;
  this->holding_reference = was_holding_reference;
  this->write_delete_held_reference(MemoryReference(this->target_register));
}

void CompilationVisitor::visit(BreakStatement* a) {
  this->file_offset = a->file_offset;

  if (this->break_label_stack.empty()) {
    throw compile_error("break statement outside loop", this->file_offset);
  }
  this->as.write_label(string_printf("__BreakStatement_%p", a));
  this->as.write_jmp(this->break_label_stack.back());
}

void CompilationVisitor::visit(ContinueStatement* a) {
  this->file_offset = a->file_offset;

  if (this->continue_label_stack.empty()) {
    throw compile_error("continue statement outside loop", this->file_offset);
  }
  this->as.write_label(string_printf("__ContinueStatement_%p", a));
  this->as.write_jmp(this->continue_label_stack.back());
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

  // it had better be a new reference if the type is nontrivial
  if (type_has_refcount(this->current_type.type) && !this->holding_reference) {
    throw compile_error("can\'t return reference to " + this->current_type.str(),
        this->file_offset);
  }

  // record this return type
  this->function_return_types.emplace(this->current_type);

  // if we're inside a finally block, there may be an active exception. but a
  // return statement inside a finally block should cause the exception to be
  // suppressed - for now we don't support this
  // TODO: implement this case
  if (this->in_finally_block) {
    throw compile_error("return statement inside finally block", this->file_offset);
  }

  // generate a jump to the end of the function (this is right before the
  // relevant destructor calls)
  // TODO: this is wrong; it doesn't cause enclosing finally blocks to execute.
  // we should unwind the exception blocks until the end of the function
  this->as.write_label(string_printf("__ReturnStatement_%p_return", a));
  this->as.write_jmp(this->return_label);
}

void CompilationVisitor::visit(RaiseStatement* a) {
  this->file_offset = a->file_offset;

  // we only support the one-argument form of the raise statement
  if (a->value.get() || a->traceback.get()) {
    throw compile_error("raise statement may only take one argument",
        this->file_offset);
  }

  // we'll construct the exception object into r15
  this->as.write_label(string_printf("__RaiseStatement_%p_evaluate_object", a));
  a->type->accept(this);

  // now jump to unwind_exception
  this->as.write_label(string_printf("__RaiseStatement_%p_unwind", a));
  this->as.write_mov(r15, MemoryReference(this->target_register));
  this->as.write_jmp(common_object_reference(void_fn_ptr(&_unwind_exception_internal)));
}

void CompilationVisitor::visit(YieldStatement* a) {
  this->file_offset = a->file_offset;

  // TODO
  throw compile_error("YieldStatement not yet implemented", this->file_offset);
}

void CompilationVisitor::visit(SingleIfStatement* a) {
  this->file_offset = a->file_offset;
  throw compile_error("SingleIfStatement used instead of subclass",
      this->file_offset);
}

void CompilationVisitor::visit(IfStatement* a) {
  this->file_offset = a->file_offset;

  // if the condition is always true, skip the condition check, elifs and else,
  // and just generate the body
  if (a->always_true) {
    this->as.write_label(string_printf("__IfStatement_%p_always_true", a));
    this->visit_list(a->items);
    return;
  }

  string false_label = string_printf("__IfStatement_%p_condition_false", a);
  string end_label = string_printf("__IfStatement_%p_end", a);

  // if the condition is always false, go directly to the elifs/else
  if (a->always_false) {
    this->as.write_label(string_printf("__IfStatement_%p_always_false", a));
  } else {
    // if the condition is always true, skip generating the condition check
    if (!a->always_true) {
      this->as.write_label(string_printf("__IfStatement_%p_condition", a));
      this->target_register = this->available_register();
      a->check->accept(this);
      this->as.write_label(string_printf("__IfStatement_%p_test", a));
      this->write_current_truth_value_test();
      this->as.write_jz(false_label);
      this->write_delete_held_reference(MemoryReference(this->target_register));

    } else {
      this->as.write_label(string_printf("__IfStatement_%p_always_true", a));
    }

    // generate the body statements, then jump to the end (skip the elifs/else
    // if the condition was true)
    this->visit_list(a->items);
    this->as.write_jmp(end_label);
  }

  // write the elif clauses
  for (auto& elif : a->elifs) {
    // if the condition is always false, just skip it entirely
    if (elif->always_false) {
      continue;
    }

    // this is where execution should resume if the previous block didn't run
    this->as.write_label(false_label);
    false_label = string_printf("__IfStatement_%p_elif_%p_condition_false",
        a, elif.get());

    // if the condition is always true, skip the condition check and just
    // generate the body. no other elifs, nor the else block
    if (elif->always_true) {
      elif->accept(this);
      this->as.write_label(end_label);
      return;
    }

    // generate the condition check
    this->as.write_label(string_printf("__IfStatement_%p_elif_%p_condition",
        a, elif.get()));
    this->target_register = this->available_register();
    elif->check->accept(this);
    this->as.write_label(string_printf("__IfStatement_%p_elif_%p_test", a,
        elif.get()));
    this->write_current_truth_value_test();
    this->as.write_jz(false_label);
    this->write_delete_held_reference(MemoryReference(this->target_register));

    // generate the body
    elif->accept(this);
    this->as.write_jmp(end_label);
  }

  // generate the else block, if any. note that we don't get here if any of the
  // if blocks were always true
  if (a->else_suite.get()) {
    this->as.write_label(false_label);
    this->write_delete_held_reference(MemoryReference(this->target_register));
    a->else_suite->accept(this);
  } else {
    this->as.write_label(false_label);
    this->write_delete_held_reference(MemoryReference(this->target_register));
  }

  this->as.write_label(end_label);
}

void CompilationVisitor::visit(ElseStatement* a) {
  this->file_offset = a->file_offset;

  // just do the sub-statements; the encapsulating logic is all in TryStatement
  // or IfStatement
  this->as.write_label(string_printf("__ElseStatement_%p", a));
  this->visit_list(a->items);
}

void CompilationVisitor::visit(ElifStatement* a) {
  this->file_offset = a->file_offset;

  // just do the sub-statements; the encapsulating logic is all in IfStatement
  this->as.write_label(string_printf("__ElifStatement_%p", a));
  this->visit_list(a->items);
}

void CompilationVisitor::visit(ForStatement* a) {
  this->file_offset = a->file_offset;

  // get the collection object and save it on the stack
  this->as.write_label(string_printf("__ForStatement_%p_get_collection", a));
  a->collection->accept(this);
  Variable collection_type = this->current_type;
  this->write_push(this->target_register);

  // we'll use rbx for some loop state (e.g. the item index in lists)
  if (this->target_register == Register::RBX) {
    throw compile_error("cannot use rbx as target register for list iteration");
  }
  this->write_push(Register::RBX);
  this->as.write_xor(rbx, rbx);

  string next_label = string_printf("__ForStatement_%p_next", a);
  string end_label = string_printf("__ForStatement_%p_complete", a);
  string break_label = string_printf("__ForStatement_%p_broken", a);

  if ((collection_type.type == ValueType::List) ||
      (collection_type.type == ValueType::Tuple)) {

    // tuples containing disparate types can't be iterated
    if ((collection_type.type == ValueType::Tuple) &&
        (!collection_type.extension_types.empty())) {
      Variable uniform_extension_type = collection_type.extension_types[0];
      for (const Variable& extension_type : collection_type.extension_types) {
        if (uniform_extension_type != extension_type) {
          string uniform_str = uniform_extension_type.str();
          string other_str = extension_type.str();
          throw compile_error(string_printf(
              "can\'t iterate over Tuple with disparate types (contains %s and %s)",
              uniform_str.c_str(), other_str.c_str()), this->file_offset);
        }
      }
    }

    ValueType item_type = collection_type.extension_types[0].type;

    this->as.write_label(next_label);
    // get the list/tuple object
    this->as.write_mov(MemoryReference(this->target_register),
        MemoryReference(Register::RSP, 8));

    // check if we're at the end and skip the body if so
    this->as.write_cmp(rbx, MemoryReference(this->target_register, 0x10));
    this->as.write_jge(end_label);

    // get the next item
    if (collection_type.type == ValueType::List) {
      this->as.write_mov(MemoryReference(this->target_register),
          MemoryReference(this->target_register, 0x28));
      if (item_type == ValueType::Float) {
        this->as.write_movq_to_xmm(this->float_target_register,
            MemoryReference(this->target_register, 0, Register::RBX, 8));
      } else {
        this->as.write_mov(MemoryReference(this->target_register),
            MemoryReference(this->target_register, 0, Register::RBX, 8));
      }
    } else {
      if (item_type == ValueType::Float) {
        this->as.write_movq_to_xmm(this->float_target_register,
            MemoryReference(this->target_register, 0x18, Register::RBX, 8));
      } else {
        this->as.write_mov(MemoryReference(this->target_register),
            MemoryReference(this->target_register, 0x18, Register::RBX, 8));
      }
    }

    // increment the item index
    this->as.write_inc(rbx);

    // if the extension type has a refcount, add a reference
    if (type_has_refcount(item_type)) {
      this->write_add_reference(this->target_register);
    }

    // load the value into the correct local variable slot
    this->as.write_label(string_printf("__ForStatement_%p_write_value", a));
    this->current_type = collection_type.extension_types[0];
    a->variable->accept(this);

    // do the loop body
    this->as.write_label(string_printf("__ForStatement_%p_body", a));
    this->break_label_stack.emplace_back(break_label);
    this->continue_label_stack.emplace_back(next_label);
    this->visit_list(a->items);
    this->continue_label_stack.pop_back();
    this->break_label_stack.pop_back();
    this->as.write_jmp(next_label);
    this->as.write_label(end_label);

  } else if (collection_type.type == ValueType::Dict) {

    int64_t previously_reserved_registers = this->write_push_reserved_registers();

    // create a SlotContents structure
    this->adjust_stack(-sizeof(DictionaryObject::SlotContents));
    this->as.write_mov(MemoryReference(Register::RSP, 0), 0);
    this->as.write_mov(MemoryReference(Register::RSP, 8), 0);
    this->as.write_mov(MemoryReference(Register::RSP, 16), 0);

    // get the dict object and SlotContents pointer
    this->as.write_label(next_label);
    this->as.write_mov(rdi,
        MemoryReference(Register::RSP, sizeof(DictionaryObject::SlotContents)));
    this->as.write_mov(rsi, rsp);

    // call dictionary_next_item
    this->write_function_call(
        common_object_reference(void_fn_ptr(&dictionary_next_item)), {rdi, rsi}, {});

    // if dictionary_next_item returned 0, then we're done
    this->as.write_test(rax, rax);
    this->as.write_je(end_label);

    // get the key pointer
    this->as.write_mov(MemoryReference(this->target_register),
        MemoryReference(Register::RSP, 0));

    // if the extension type has a refcount, add a reference
    if (type_has_refcount(collection_type.extension_types[0].type)) {
      this->write_add_reference(this->target_register);
    }

    // load the value into the correct local variable slot
    this->as.write_label(string_printf("__ForStatement_%p_write_key_value", a));
    this->current_type = collection_type.extension_types[0];
    a->variable->accept(this);

    // do the loop body
    this->as.write_label(string_printf("__ForStatement_%p_body", a));
    this->break_label_stack.emplace_back(break_label);
    this->continue_label_stack.emplace_back(next_label);
    this->visit_list(a->items);
    this->continue_label_stack.pop_back();
    this->break_label_stack.pop_back();
    this->as.write_jmp(next_label);
    this->as.write_label(end_label);

    // clean up the stack
    this->adjust_stack(sizeof(DictionaryObject::SlotContents));
    this->write_pop_reserved_registers(previously_reserved_registers);

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

  // restore rbx
  this->write_pop(Register::RBX);

  // we still own a reference to the collection; destroy it now
  // note: all collection types have refcounts; dunno why I bothered checking
  if (type_has_refcount(collection_type.type)) {
    this->write_pop(this->target_register);
    this->write_delete_reference(MemoryReference(this->target_register),
        collection_type.type);
  } else {
    this->as.write_add(rsp, 8);
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
  this->write_current_truth_value_test();
  this->as.write_jz(end_label);

  // generate the loop body
  this->write_delete_held_reference(MemoryReference(this->target_register));
  this->as.write_label(string_printf("__WhileStatement_%p_body", a));
  this->break_label_stack.emplace_back(break_label);
  this->continue_label_stack.emplace_back(start_label);
  this->visit_list(a->items);
  this->continue_label_stack.pop_back();
  this->break_label_stack.pop_back();
  this->as.write_jmp(start_label);

  // when the loop ends normally, we may still be holding a reference to the
  // condition result
  this->as.write_label(end_label);
  this->write_delete_held_reference(MemoryReference(this->target_register));

  // if there's an else statement, generate the body here
  if (a->else_suite.get()) {
    a->else_suite->accept(this);
  }

  // any break statement will jump over the loop body and the else statement
  this->as.write_label(break_label);
}

void CompilationVisitor::visit(ExceptStatement* a) {
  this->file_offset = a->file_offset;

  // just do the sub-statements; the encapsulating logic is all in TryStatement
  this->as.write_label(string_printf("__FinallyStatement_%p", a));
  this->visit_list(a->items);
}

void CompilationVisitor::visit(FinallyStatement* a) {
  this->file_offset = a->file_offset;

  bool prev_in_finally_block = this->in_finally_block;
  this->in_finally_block = true;

  // we save the active exception and clear it, so the finally block can contain
  // further try/except blocks without clobbering the active exception
  this->as.write_label(string_printf("__FinallyStatement_%p_save_exc", a));
  this->write_push(Register::R15);
  this->as.write_xor(r15, r15);

  this->as.write_label(string_printf("__FinallyStatement_%p_body", a));
  this->visit_list(a->items);

  // if there's now an active exception, then the finally block raised an
  // exception of its own. if there was a saved exception object, destroy it;
  // then start unwinding the new exception
  string no_exc_label = string_printf("__FinallyStatement_%p_no_exc", a);
  string end_label = string_printf("__FinallyStatement_%p_end", a);
  this->as.write_label(string_printf("__FinallyStatement_%p_restore_exc", a));
  this->as.write_test(r15, r15);
  this->as.write_jz(no_exc_label);
  this->write_delete_reference(MemoryReference(Register::R15, 0), ValueType::Instance);
  this->as.write_jmp(common_object_reference(void_fn_ptr(&_unwind_exception_internal)));

  // the finally block did not raise an exception, but there may be a saved
  // exception. if so, unwind it now
  this->as.write_label(no_exc_label);
  this->write_pop(Register::R15);
  this->as.write_test(r15, r15);
  this->as.write_jz(end_label);
  this->as.write_jmp(common_object_reference(void_fn_ptr(&_unwind_exception_internal)));
  this->as.write_label(end_label);

  this->in_finally_block = prev_in_finally_block;
}

void CompilationVisitor::visit(TryStatement* a) {
  this->file_offset = a->file_offset;

  // here's how we implement exception handling:

  // # all try blocks have a finally block, even if it's not defined in the code
  // # let N be the number of except clauses on the try block
  // try:
  //   # stack-allocate one exception block on the stack with exc_class_id = 0,
  //   #     pointing to the finally block, and containing N except clause
  //   #     blocks pointing to each except block's code
  //   if should_raise:
  //     raise KeyError()  # allocate object, set r15, call unwind_exception
  //   # remove the exception block from the stack
  //   # if there's an else block, jump there
  //   # if there's a finally block, jump there
  //   # jump to end of suite chain
  // except KeyError as e:
  //   # let this exception block's index be I
  //   # if the exception has a name (is assigned to a local variable), then
  //   #     write r15 to that variable; else, delete the object pointed to by
  //   #     r15 and clear r15
  //   # note: don't remove the exception block from the stack; this was already
  //   #     done by unwind_exception
  //   print('caught KeyError')
  //   # if there's a finally block, jump there
  //   # jump to end of suite chain
  // else:
  //   print('did not catch KeyError')
  //   # if there's a finally block, jump there
  //   # jump to end of suite chain
  // finally:
  //   # note: we can get here with an active exception (r15 != 0)
  //   print('executed finally block')
  //   # if r15 is nonzero, call unwind_exception again

  this->as.write_label(string_printf("__TryStatement_%p_create_exc_block", a));

  // we jump here from other functions, so don't let any registers be reserved
  int64_t previously_reserved_registers = this->write_push_reserved_registers();

  // generate the exception block
  string finally_label = string_printf("__TryStatement_%p_finally", a);
  vector<pair<string, unordered_set<int64_t>>> label_to_class_ids;
  for (size_t x = 0; x < a->excepts.size(); x++) {
    string label = string_printf("__TryStatement_%p_except_%zd", a, x);
    label_to_class_ids.emplace_back(make_pair(label, a->excepts[x]->class_ids));
  }
  size_t stack_bytes_used_on_restore = this->stack_bytes_used;
  this->write_create_exception_block(label_to_class_ids, finally_label);

  // generate the try block body
  this->as.write_label(string_printf("__TryStatement_%p_body", a));
  this->visit_list(a->items);

  // remove the exception block from the stack
  // the previous exception block pointer is the first field in the exception
  // block, so load r14 from there
  this->as.write_label(string_printf("__TryStatement_%p_remove_exc_blocks", a));
  this->as.write_mov(r14, MemoryReference(Register::RSP, 0));
  this->adjust_stack_to(stack_bytes_used_on_restore);

  // generate the else block if there is one. the exc block is already gone, so
  // this code isn't covered by the except clauses, but we need to generate a
  // new exc block to make this be covered by the finally clause
  if (a->else_suite.get()) {
    this->as.write_label(string_printf("__TryStatement_%p_create_else_exc_block", a));
    this->write_create_exception_block({}, finally_label);
    a->else_suite->accept(this);
    this->as.write_label(string_printf("__TryStatement_%p_delete_else_exc_block", a));
    this->as.write_mov(r14, MemoryReference(Register::RSP, 0));
    this->adjust_stack_to(stack_bytes_used_on_restore);
  }

  // go to the finally block
  this->as.write_jmp(finally_label);

  // generate the except blocks
  for (size_t except_index = 0; except_index < a->excepts.size(); except_index++) {
    const auto& except = a->excepts[except_index];

    this->as.write_label(string_printf("__TryStatement_%p_except_%zd", a,
        except_index));

    // adjust our stack offset tracking appropriately. we don't write the opcode
    // because the stack has already been set to this offset by
    // _unwind_exception_internal; we just need to keep track of it so we can
    // avoid unaligned function calls
    this->adjust_stack_to(stack_bytes_used_on_restore, false);

    // if the exception object isn't assigned to a name, destroy it now
    if (except->name.empty()) {
      this->write_delete_reference(r15, ValueType::Instance);

    // else, assign the exception object to the appropriate name
    } else {
      this->as.write_label(string_printf("__TryStatement_%p_except_%zd_write_value",
          a, except_index));

      VariableLocation loc = this->location_for_variable(except->name);

      // typecheck the result
      // TODO: deduplicate some of this with AttributeLValueReference
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
        throw compile_error("target variable not found in exception block");
      }

      if ((target_variable->type != ValueType::Instance) ||
          !except->class_ids.count(target_variable->class_id)) {
        throw compile_error(string_printf("variable %s is not an exception instance type",
            loc.name.c_str()));
      }

      // delete the old value if present, save the new value, and clear the active
      // exception pointer
      this->write_delete_reference(loc.mem, loc.type.type);
      this->as.write_mov(loc.mem, r15);
    }

    // clear the active exception
    this->as.write_xor(r15, r15);

    // generate the except block code
    this->as.write_label(string_printf("__TryStatement_%p_except_%zd_body",
        a, except_index));
    except->accept(this);

    // we're done here; go to the finally block
    // for the last except block, don't bother jumping; just fall through
    if (except_index != a->excepts.size() - 1) {
      this->as.write_label(string_printf("__TryStatement_%p_except_%zd_end", a,
          except_index));
      this->as.write_jmp(string_printf("__TryStatement_%p_finally", a));
    }
  }

  // now we're back to the initial stack offset
  this->adjust_stack_to(stack_bytes_used_on_restore, false);

  // generate the finally block, if any
  this->as.write_label(string_printf("__TryStatement_%p_finally", a));
  if (a->finally_suite.get()) {
    a->finally_suite->accept(this);
  }

  this->write_pop_reserved_registers(previously_reserved_registers);
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

  // if the function is __init__, implicitly return self (the function cannot
  // explicitly return a value)
  if (this->target_function->is_class_init()) {
    // the value should be returned in rax
    this->as.write_label(string_printf("__FunctionDefinition_%p_return_self_from_init", a));

    VariableLocation loc = this->location_for_variable("self");

    // add a reference to self and load it into rax for returning
    this->target_register = Register::RAX;
    this->as.write_mov(MemoryReference(this->target_register), loc.mem);
    if (!type_has_refcount(loc.type.type)) {
      throw compile_error("self is not an object", this->file_offset);
    }
    this->write_add_reference(this->target_register);
  }

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
    // if none of the class attributes have destructors and it doesn't have a
    // __del__ method, then the overall class destructor trivializes to free()
    bool has_del = cls->attributes.count("__del__");
    bool has_subdestructors = has_del;
    if (!has_subdestructors) {
      for (const auto& it : cls->attributes) {
        if (type_has_refcount(it.second.type)) {
          has_subdestructors = true;
          break;
        }
      }
    }

    // no subdestructors; just use free()
    if (!has_subdestructors) {
      cls->destructor = reinterpret_cast<void*>(&free);
      if (debug_flags & DebugFlag::ShowAssembly) {
        fprintf(stderr, "[%s.%s:%" PRId64 "] class has trivial destructor\n",
            this->module->name.c_str(), a->name.c_str(), a->class_id);
      }

    // there are subdestructors; have to write a destructor function
    } else {
      string base_label = string_printf("__ClassDefinition_%p_destructor", a);
      AMD64Assembler dtor_as;
      dtor_as.write_label(base_label);

      // lead-in (stack frame setup)
      dtor_as.write_push(Register::RBP);
      dtor_as.write_mov(rbp, rsp);

      // we'll keep the object pointer in rbx since it's callee-save
      dtor_as.write_push(Register::RBX);
      dtor_as.write_mov(rbx, rdi);

      // make sure the stack is aligned at call time for any subfunctions
      dtor_as.write_sub(rsp, 8);

      // we have to add a fake reference to the object while destroying it;
      // otherwise __del__ will call this destructor recursively
      dtor_as.write_lock();
      dtor_as.write_inc(MemoryReference(Register::RBX, 0));

      // call __del__ before deleting attribute references
      if (has_del) {
        // figure out what __del__ is
        auto& del_attr = cls->attributes.at("__del__");
        if (del_attr.type != ValueType::Function) {
          throw compile_error("__del__ exists but is not a function; instead it\'s " + del_attr.str(), this->file_offset);
        }
        if (!del_attr.value_known) {
          throw compile_error("__del__ exists but is an unknown value", this->file_offset);
        }

        // the arg signature is blank since __del__ can't take any arguments
        auto* fn = this->global->context_for_function(del_attr.function_id);
        int64_t fragment_id = fn->arg_signature_to_fragment_id.emplace(
            "", fn->fragments.size()).first->second;

        // get or generate the Fragment object
        const FunctionContext::Fragment* fragment;
        try {
          fragment = &fn->fragments.at(fragment_id);
        } catch (const std::out_of_range& e) {
          unordered_map<string, Variable> local_overrides;
          local_overrides.emplace("self", Variable(ValueType::Instance, a->class_id, NULL));
          auto new_fragment = this->global->compile_scope(fn->module, fn,
              &local_overrides);
          fragment = &fn->fragments.emplace(fragment_id, move(new_fragment)).first->second;
        }

        // generate the call to the fragment. note that the instance pointer is
        // still in rdi, so we don't have to do anything to prepare
        dtor_as.write_lock();
        dtor_as.write_inc(MemoryReference(Register::RBX, 0)); // reference for the function arg
        dtor_as.write_mov(Register::RAX,
            reinterpret_cast<int64_t>(fragment->compiled));
        dtor_as.write_call(rax);

        // __del__ can add new references to the object; if this happens, don't
        // proceed with the destruction
        // TODO: if the refcpount is zero, something has gone seriously wrong.
        // what should we do in that case?
        // TODO: do we need the lock prefix to do this compare?
        dtor_as.write_cmp(MemoryReference(Register::RBX, 0), 1);
        dtor_as.write_je(base_label + "_proceed");
        dtor_as.write_lock();
        dtor_as.write_dec(MemoryReference(Register::RBX, 0)); // fake reference
        dtor_as.write_add(rsp, 8);
        dtor_as.write_pop(Register::RBX);
        dtor_as.write_ret();
        dtor_as.write_label(base_label + "_proceed");
      }

      // the first 2 fields are the refcount and destructor pointer
      // the rest are the attributes, in the same order as in the attributes map
      for (const auto& it : cls->dynamic_attribute_indexes) {
        const string& attr_name = it.first;
        size_t offset = cls->offset_for_attribute(it.second);

        auto& attr = cls->attributes.at(attr_name);
        if (type_has_refcount(attr.type)) {
          // write a destructor call
          dtor_as.write_label(string_printf("%s_delete_reference_%s", base_label.c_str(),
              attr_name.c_str()));

          // if inline refcounting is disabled, call delete_reference manually
          if (debug_flags & DebugFlag::NoInlineRefcounting) {
            dtor_as.write_mov(rdi, MemoryReference(Register::RBX, offset));
            dtor_as.write_mov(rsi, r14);
            dtor_as.write_call(common_object_reference(void_fn_ptr(&delete_reference)));

          } else {
            string skip_label = string_printf(
                "__destructor_delete_reference_skip_%" PRIu64, offset);

            // get the object pointer
            MemoryReference rdi(Register::RDI);
            dtor_as.write_mov(rdi, MemoryReference(Register::RBX, offset));

            // if the pointer is NULL, do nothing
            dtor_as.write_test(rdi, rdi);
            dtor_as.write_je(skip_label);

            // decrement the refcount; if it's not zero, skip the destructor call
            dtor_as.write_lock();
            dtor_as.write_dec(MemoryReference(Register::RDI, 0));
            dtor_as.write_jnz(skip_label);

            // call the destructor
            dtor_as.write_mov(rax, MemoryReference(Register::RDI, 8));
            dtor_as.write_call(rax);

            dtor_as.write_label(skip_label);
          }
        }
      }

      dtor_as.write_label(base_label + "_jmp_free");

      // remove the fake reference to the object being destroyed. if anyone else
      // added a reference in the meantime (e.g. while attributes were being
      // destroyed), then they're holding a reference to an incomplete object
      // and they deserve the segfault they will probably get
      dtor_as.write_lock();
      dtor_as.write_dec(MemoryReference(Register::RBX, 0));

      // cheating time: "return" by jumping directly to free() so it will return
      // to the caller
      dtor_as.write_mov(rdi, rbx);
      dtor_as.write_add(rsp, 8);
      dtor_as.write_pop(Register::RBX);
      dtor_as.write_mov(rbp, common_object_reference(void_fn_ptr(&free)));
      dtor_as.write_xchg(Register::RBP, MemoryReference(Register::RSP, 0));
      dtor_as.write_ret();

      // assemble it
      multimap<size_t, string> compiled_labels;
      unordered_set<size_t> patch_offsets;
      string compiled = dtor_as.assemble(patch_offsets, &compiled_labels);
      cls->destructor = this->global->code.append(compiled, &patch_offsets);
      this->module->compiled_size += compiled.size();

      if (debug_flags & DebugFlag::ShowAssembly) {
        fprintf(stderr, "[%s:%" PRId64 "] class destructor assembled\n",
            a->name.c_str(), a->class_id);
        uint64_t addr = reinterpret_cast<uint64_t>(cls->destructor);
        string disassembly = AMD64Assembler::disassemble(cls->destructor,
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
  this->current_type = value.type_only();

  switch (value.type) {
    case ValueType::Indeterminate:
      throw compile_error("can\'t generate code for Indeterminate value", this->file_offset);

    case ValueType::None:
      this->as.write_xor(MemoryReference(this->target_register),
          MemoryReference(this->target_register));
      break;

    case ValueType::Bool:
    case ValueType::Int:
      this->as.write_mov(this->target_register, value.int_value);
      break;

    case ValueType::Float:
      this->write_load_double(this->float_target_register, value.float_value);
      break;

    case ValueType::Bytes:
    case ValueType::Unicode: {
      const void* o = (value.type == ValueType::Bytes) ?
          void_fn_ptr(this->global->get_or_create_constant(*value.bytes_value)) :
          void_fn_ptr(this->global->get_or_create_constant(*value.unicode_value));
      this->as.write_mov(this->target_register, reinterpret_cast<int64_t>(o));
      this->write_add_reference(this->target_register);
      this->holding_reference = true;
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

void CompilationVisitor::assert_not_evaluating_instance_pointer() {
  if (this->evaluating_instance_pointer) {
    throw compile_error("incorrect node visited when evaluating instance pointer",
        this->file_offset);
  }
}

ssize_t CompilationVisitor::write_function_call_stack_prep(size_t arg_count) {
  ssize_t arg_stack_bytes = (arg_count > int_argument_register_order.size()) ?
      ((arg_count - int_argument_register_order.size()) * sizeof(int64_t)) : 0;

  // make sure the stack will be aligned at call time
  arg_stack_bytes += ((this->stack_bytes_used + arg_stack_bytes) & 0x0F);
  if (arg_stack_bytes) {
    this->adjust_stack(-arg_stack_bytes);
  }

  return arg_stack_bytes;
}

void CompilationVisitor::write_function_call(
    const MemoryReference& function_loc,
    const std::vector<const MemoryReference>& int_args,
    const std::vector<const MemoryReference>& float_args,
    ssize_t arg_stack_bytes, Register return_register, bool return_float) {

  if (float_args.size() > 8) {
    // TODO: we should support this in the future. probably we just stuff them
    // into the stack somewhere, but need to figure out exactly where/how
    throw compile_error("cannot call functions with more than 8 floating-point arguments",
        this->file_offset);
  }

  int64_t previously_reserved_registers = this->write_push_reserved_registers();

  size_t rsp_adjustment = 0;
  if (arg_stack_bytes < 0) {
    arg_stack_bytes = this->write_function_call_stack_prep(int_args.size());
    // if any of the references are memory references based on RSP, we'll have
    // to adjust them
    rsp_adjustment = arg_stack_bytes;
  }

  // generate the list of move destinations
  vector<MemoryReference> dests;
  for (size_t x = 0; x < int_args.size(); x++) {
    if (x < int_argument_register_order.size()) {
      dests.emplace_back(int_argument_register_order[x]);
    } else {
      dests.emplace_back(Register::RSP, (x - int_argument_register_order.size()) * 8);
    }
  }

  // deal with conflicting moves by making a graph of the moves. in this graph,
  // there's an edge from m1 to m2 if m1.src == m2.dest. this means m1 has to be
  // done before m2 to maintain correct values. then we can just do a
  // topological sort on this graph and do the moves in that order. but watch
  // out: the graph can have cycles, and we'll have to break them somehow,
  // probably by using stack space
  unordered_map<size_t, unordered_set<size_t>> move_to_dependents;
  for (size_t x = 0; x < int_args.size(); x++) {
    for (size_t y = 0; y < int_args.size(); y++) {
      if (x == y) {
        continue;
      }
      if (int_args[x] == dests[y]) {
        move_to_dependents[x].insert(y);
      }
    }
  }

  // DFS-based topological sort. for now just fail if a cycle is detected.
  // TODO: deal with cycles somehow
  deque<size_t> move_order;
  vector<bool> moves_considered(int_args.size(), false);
  vector<bool> moves_in_progress(int_args.size(), false);
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
  for (size_t x = 0; x < int_args.size(); x++) {
    if (moves_considered[x]) {
      continue;
    }
    visit_move(x);
  }

  // generate the mov opcodes in the determined order
  for (size_t arg_index : move_order) {
    auto& ref = int_args[arg_index];
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

  // generate the appropriate floating mov opcodes
  // TODO: we need to topological sort these too, sigh
  for (size_t arg_index = 0; arg_index < float_args.size(); arg_index++) {
    auto& ref = float_args[arg_index];
    MemoryReference dest(float_argument_register_order[arg_index]);
    if (ref == dest) {
      continue;
    }
    if (!ref.field_size) {
      this->as.write_movq_to_xmm(dest.base_register, ref);
    } else if (ref.base_register == Register::RSP) {
      MemoryReference new_ref(ref.base_register, ref.offset + rsp_adjustment,
          ref.index_register, ref.field_size);
      this->as.write_movsd(dest, new_ref);
    } else {
      this->as.write_movsd(dest, ref);
    }
  }

  // finally, call the function. the stack must be 16-byte aligned at this point
  if (this->stack_bytes_used & 0x0F) {
    throw compile_error("stack not aligned at function call");
  }
  this->as.write_call(function_loc);

  // put the return value into the target register
  if (return_float) {
    if ((return_register != Register::None) &&
        (return_register != Register::XMM0)) {
      this->as.write_movsd(MemoryReference(return_register), xmm0);
    }
  } else {
    if ((return_register != Register::None) &&
      (return_register != Register::RAX)) {
      this->as.write_mov(MemoryReference(return_register), rax);
    }
  }

  // reclaim any reserved stack space
  if (arg_stack_bytes) {
    this->adjust_stack(arg_stack_bytes);
  }

  this->write_pop_reserved_registers(previously_reserved_registers);
}

void CompilationVisitor::write_function_setup(const string& base_label) {
  // get ready to rumble
  this->as.write_label("__" + base_label);
  this->stack_bytes_used = 8;

  // lead-in (stack frame setup)
  this->write_push(Register::RBP);
  this->as.write_mov(rbp, rsp);

  // reserve space for locals and write args into the right places
  unordered_map<string, Register> int_arg_to_register;
  unordered_map<string, int64_t> int_arg_to_stack_offset;
  unordered_map<string, Register> float_arg_to_register;
  size_t arg_stack_offset = sizeof(int64_t) * 2; // after return addr and rbp
  for (size_t arg_index = 0; arg_index < this->target_function->args.size(); arg_index++) {
    const auto& arg = this->target_function->args[arg_index];

    bool is_float;
    try {
      is_float = this->local_overrides.at(arg.name).type == ValueType::Float;
    } catch (const out_of_range&) {
      throw compile_error(string_printf("argument %s not present in local_overrides",
          arg.name.c_str()), this->file_offset);
    }

    if (is_float) {
      if (float_arg_to_register.size() >= float_argument_register_order.size()) {
        throw compile_error("function accepts too many float args", this->file_offset);
      }
      float_arg_to_register.emplace(arg.name,
          float_argument_register_order[float_arg_to_register.size()]);

    } else if (int_arg_to_register.size() < int_argument_register_order.size()) {
      int_arg_to_register.emplace(arg.name,
          int_argument_register_order[int_arg_to_register.size()]);

    } else {
      // add 2, since at this point we've called the function and pushed RBP
      int_arg_to_stack_offset.emplace(arg.name, arg_stack_offset);
      arg_stack_offset += sizeof(int64_t);
    }
  }

  // set up the local space
  for (const auto& local : this->target_function->locals) {

    // if it's a float arg, reserve stack space and write it from the xmm reg
    try {
      Register xmm_reg = float_arg_to_register.at(local.first);
      MemoryReference xmm_mem(xmm_reg);
      this->adjust_stack(-8);
      this->as.write_movsd(MemoryReference(Register::RSP, 0), xmm_mem);
      continue;
    } catch (const out_of_range&) { }

    // if it's a register arg, push it directly
    try {
      this->write_push(int_arg_to_register.at(local.first));
      continue;
    } catch (const out_of_range&) { }

    // if it's a stack arg, push it via r/m push
    try {
      this->write_push(MemoryReference(Register::RBP,
          int_arg_to_stack_offset.at(local.first)));
      continue;
    } catch (const out_of_range&) { }

    // else, initialize it to zero
    this->write_push(0);
  }

  // set up the exception block
  this->return_label = string_printf("__%s_return", base_label.c_str());
  this->exception_return_label = string_printf(
      "__%s_exception_return", base_label.c_str());
  this->as.write_label(string_printf(
      "__%s_create_except_block", base_label.c_str()));
  this->write_create_exception_block({}, this->exception_return_label);
}

void CompilationVisitor::write_function_cleanup(const string& base_label) {
  this->as.write_label(this->return_label);

  // clean up the exception block. note that this is after the return label but
  // before the destroy locals label - the latter is used when an exception
  // occurs, since _unwind_exception_internal already removes the exc block from
  // the stack
  this->write_pop(Register::R14);
  this->adjust_stack(return_exception_block_size - sizeof(int64_t));

  // call destructors for all the local variables that have refcounts
  this->as.write_label(this->exception_return_label);
  this->return_label.clear();
  this->exception_return_label.clear();
  for (auto it = this->target_function->locals.crbegin();
       it != this->target_function->locals.crend(); it++) {
    if (type_has_refcount(it->second.type)) {
      // we have to preserve the value in rax since it's the function's return
      // value, so store it on the stack (in the location we're about to pop)
      // while we destroy the object
      this->as.write_xchg(Register::RAX, MemoryReference(Register::RSP, 0));
      this->write_delete_reference(rax, it->second.type);
      this->write_pop(Register::RAX);
    } else {
      // no destructor; just skip it
      // TODO: we can coalesce these adds for great justice
      this->adjust_stack(8);
    }
  }

  // hooray we're done
  this->as.write_label(string_printf("__%s_leave_frame", base_label.c_str()));
  this->write_pop(Register::RBP);

  if (this->stack_bytes_used != 8) {
    throw compile_error(string_printf(
        "stack misaligned at end of function (%" PRId64 " bytes used; should be 8)",
        this->stack_bytes_used), this->file_offset);
  }

  this->as.write_ret();
}

void CompilationVisitor::write_add_reference(Register addr_reg) {
  if (debug_flags & DebugFlag::NoInlineRefcounting) {
    this->reserve_register(addr_reg);
    this->write_function_call(common_object_reference(void_fn_ptr(&add_reference)),
        {MemoryReference(addr_reg)}, {});
    this->release_register(addr_reg);
  } else {
    this->as.write_lock();
    this->as.write_inc(MemoryReference(addr_reg, 0));
  }
  // TODO: we should check if the value is 1. if it is, then we've encountered a
  // data race, and another thread is currently deleting this object!
}

void CompilationVisitor::write_delete_held_reference(const MemoryReference& mem) {
  if (this->holding_reference) {
    if (!type_has_refcount(this->current_type.type)) {
      throw compile_error("holding a reference to a trivial type: " + this->current_type.str(), this->file_offset);
    }
    this->write_delete_reference(mem, this->current_type.type);
    this->holding_reference = false;
  }
}

void CompilationVisitor::write_delete_reference(const MemoryReference& mem,
    ValueType type) {
  if (type == ValueType::Indeterminate) {
    throw compile_error("can\'t call destructor for Indeterminate value", this->file_offset);
  }

  if (type_has_refcount(type)) {
    if (debug_flags & DebugFlag::NoInlineRefcounting) {
      this->write_function_call(common_object_reference(void_fn_ptr(&delete_reference)),
          {mem, r14}, {});

    } else {
      static uint64_t skip_label_id = 0;
      string skip_label = string_printf("__delete_reference_skip_%" PRIu64,
          skip_label_id++);
      Register r = this->available_register();
      MemoryReference r_mem(r);

      // get the object pointer
      if (mem.field_size || (r != mem.base_register)) {
        this->as.write_mov(r_mem, mem);
      }

      // if the pointer is NULL, do nothing
      this->as.write_test(r_mem, r_mem);
      this->as.write_je(skip_label);

      // decrement the refcount; if it's not zero, skip the destructor call
      this->as.write_lock();
      this->as.write_dec(MemoryReference(r, 0));
      this->as.write_jnz(skip_label);

      // call the destructor
      MemoryReference function_loc(r, 8);
      this->write_function_call(function_loc, {r_mem}, {});

      this->as.write_label(skip_label);
    }
  }
}

void CompilationVisitor::write_alloc_class_instance(int64_t class_id,
    bool initialize_attributes) {
  auto* cls = this->global->context_for_class(class_id);

  static uint64_t skip_label_id = 0;
  string skip_label = string_printf("__alloc_class_instance_skip_%" PRIu64,
      skip_label_id++);

  // call malloc to create the class object. note that the stack is already
  // adjusted to the right alignment here
  // note: this is a semi-ugly hack, but we ignore reserved registers here
  // because this can only be the first argument - no registers can be
  // reserved at this point
  int64_t stack_bytes_used = this->write_function_call_stack_prep();
  this->as.write_mov(rdi, cls->instance_size());
  this->as.write_call(common_object_reference(void_fn_ptr(&malloc)));
  this->adjust_stack(stack_bytes_used);

  // check if the result is NULL and raise MemoryError in that case
  this->as.write_test(rax, rax);
  this->as.write_jnz(skip_label);
  this->as.write_mov(rax, common_object_reference(&MemoryError_instance));
  this->write_add_reference(Register::RAX);
  this->as.write_mov(r15, rax);
  this->as.write_jmp(
      common_object_reference(void_fn_ptr(&_unwind_exception_internal)));
  this->as.write_label(skip_label);

  // fill in the refcount, destructor function and class id
  Register tmp = this->available_register_except({Register::RAX});
  MemoryReference tmp_mem(tmp);
  if (this->target_register != Register::RAX) {
    this->as.write_mov(MemoryReference(this->target_register), rax);
  }
  this->as.write_mov(MemoryReference(this->target_register, 0), 1);
  this->as.write_mov(tmp, reinterpret_cast<int64_t>(cls->destructor));
  this->as.write_mov(MemoryReference(this->target_register, 8), tmp_mem);
  this->as.write_mov(MemoryReference(this->target_register, 16), class_id);

  // zero everything else in the class, if it has any attributes
  if (initialize_attributes && (cls->instance_size() != sizeof(InstanceObject))) {
    this->as.write_xor(tmp_mem, tmp_mem);
    for (size_t x = sizeof(InstanceObject); x < cls->instance_size(); x += 8) {
      this->as.write_mov(MemoryReference(this->target_register, x), tmp_mem);
    }
  }
}

void CompilationVisitor::write_raise_exception(int64_t class_id) {
  auto* cls = this->global->context_for_class(class_id);

  // this form can only be used for exceptions that don't take an argument
  if (cls->instance_size() != sizeof(InstanceObject)) {
    throw compile_error("incorrect exception raise form generated");
  }

  this->write_alloc_class_instance(class_id, false);
  this->as.write_mov(r15, MemoryReference(this->target_register));

  // TODO: implement form of this function that zeroes attributes (like below)
  // and calls __init__ appropriately
  // zero everything else in the class
  //this->as.write_xor(rax, rax);
  //for (size_t x = sizeof(InstanceObject); x < cls->instance_size(); x += 8) {
  //  this->as.write_mov(MemoryReference(Register::R15, x), rax);
  //}

  // raise the exception
  this->as.write_jmp(common_object_reference(void_fn_ptr(&_unwind_exception_internal)));
}

void CompilationVisitor::write_create_exception_block(
    const vector<pair<string, unordered_set<int64_t>>>& label_to_class_ids,
    const string& exception_return_label) {
  Register tmp_rsp = this->available_register();
  Register tmp = this->available_register_except({tmp_rsp});
  MemoryReference tmp_mem(tmp);

  this->as.write_mov(MemoryReference(tmp_rsp), rsp);

  this->write_push(0);
  this->as.write_mov(tmp, exception_return_label);
  this->write_push(tmp);

  // label_to_class_ids is in the order that the except blocks are declared in
  // the file, so we need to push them in the opposite order (so the first one
  // appears earliest in memory and will match first)
  for (auto it = label_to_class_ids.rbegin(); it != label_to_class_ids.rend(); it++) {
    const string& target_label = it->first;
    const auto& class_ids = it->second;

    if (class_ids.size() == 0) {
      throw compile_error("non-finally block contained zero class ids",
          this->file_offset);
    }

    for (int64_t class_id : class_ids) {
      this->write_push(class_id);
    }
    this->write_push(class_ids.size());
    this->as.write_mov(tmp, target_label);
    this->write_push(tmp);
  }

  this->write_push(Register::R13);
  this->write_push(Register::R12);

  this->write_push(Register::RBP);
  this->write_push(tmp_rsp);
  this->write_push(Register::R14);
  this->as.write_mov(r14, rsp);
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

void CompilationVisitor::adjust_stack(ssize_t bytes, bool write_opcode) {
  if (!bytes) {
    return;
  }

  if (write_opcode) {
    if (bytes < 0) {
      this->as.write_sub(rsp, -bytes);
    } else {
      this->as.write_add(rsp, bytes);
    }
  }

  this->stack_bytes_used -= bytes;
}

void CompilationVisitor::adjust_stack_to(ssize_t bytes, bool write_opcode) {
  this->adjust_stack(this->stack_bytes_used - bytes, write_opcode);
}

void CompilationVisitor::write_load_double(Register reg, double value) {
  Register tmp = this->available_register();
  const int64_t* int_value = reinterpret_cast<const int64_t*>(&value);
  this->as.write_mov(tmp, *int_value);
  this->as.write_movq_to_xmm(this->float_target_register, MemoryReference(tmp));
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
  if (this->target_function &&
      this->target_function->explicit_globals.count(name) &&
      this->target_function->locals.count(name)) {
    throw compile_error("explicit global is also a local", this->file_offset);
  }
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

  VariableLocation loc;
  loc.name = name;
  loc.is_global = false;
  try {
    loc.mem = MemoryReference(instance_reg, cls->offset_for_attribute(name.c_str()));
  } catch (const out_of_range& e) {
    throw compile_error("cannot generate lookup for non-dynamic attribute: " + name,
        this->file_offset);
  }
  loc.type = cls->attributes.at(name);
  return loc;
}
