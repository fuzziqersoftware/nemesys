#include "CompilationVisitor.hh"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <libamd64/AMD64Assembler.hh>

#include "../Debug.hh"
#include "../AST/PythonLexer.hh"
#include "../AST/PythonParser.hh"
#include "../AST/PythonASTNodes.hh"
#include "../AST/PythonASTVisitor.hh"
#include "../Environment/Value.hh"
#include "../Types/Reference.hh"
#include "../Types/Strings.hh"
#include "../Types/Format.hh"
#include "../Types/List.hh"
#include "../Types/Tuple.hh"
#include "../Types/Dictionary.hh"
#include "CommonObjects.hh"
#include "Exception.hh"
#include "BuiltinFunctions.hh"
#include "Compile.hh"

using namespace std;



static const vector<Register> int_argument_register_order = {
    Register::RDI, Register::RSI, Register::RDX, Register::RCX, Register::R8,
    Register::R9};
static const vector<Register> float_argument_register_order = {
    Register::XMM0, Register::XMM1, Register::XMM2, Register::XMM3,
    Register::XMM4, Register::XMM5, Register::XMM6, Register::XMM7};

static const int64_t default_available_int_registers =
    (1 << Register::RAX) | (1 << Register::RCX) | (1 << Register::RDX) |
    (1 << Register::RSI) | (1 << Register::RDI) | (1 << Register::R8) |
    (1 << Register::R9) | (1 << Register::R10) | (1 << Register::R11);
static const int64_t default_available_float_registers = 0xFFFF; // all of them



CompilationVisitor::terminated_by_split::terminated_by_split(
    int64_t callsite_token) : runtime_error("terminated by split"),
    callsite_token(callsite_token) { }



CompilationVisitor::CompilationVisitor(GlobalContext* global,
    ModuleContext* module, Fragment* fragment) : file_offset(-1),
    global(global), module(module), fragment(fragment),
    available_int_registers(default_available_int_registers),
    available_float_registers(default_available_float_registers),
    target_register(rax), float_target_register(xmm0), stack_bytes_used(0),
    holding_reference(false), evaluating_instance_pointer(false),
    in_finally_block(false) {

  if (this->fragment->function) {
    if (this->fragment->function->args.size() != this->fragment->arg_types.size()) {
      throw compile_error("fragment and function take different argument counts", this->file_offset);
    }

    // populate local_variable_types with the argument types
    for (size_t x = 0; x < this->fragment->arg_types.size(); x++) {
      this->local_variable_types.emplace(this->fragment->function->args[x].name,
          this->fragment->arg_types[x]);
    }

    // populate the rest of the locals
    for (const auto& it : this->fragment->function->locals) {
      this->local_variable_types.emplace(it.first, it.second);
    }

    // clear the split labels and offsets
    this->fragment->call_split_offsets.resize(this->fragment->function->num_splits);
    this->fragment->call_split_labels.resize(this->fragment->function->num_splits);
  } else {
    this->fragment->call_split_offsets.resize(this->module->root_fragment_num_splits);
    this->fragment->call_split_labels.resize(this->module->root_fragment_num_splits);
  }

  for (size_t x = 0; x < this->fragment->call_split_offsets.size(); x++) {
    this->fragment->call_split_offsets[x] = -1;
    this->fragment->call_split_labels[x].clear();
  }
}

AMD64Assembler& CompilationVisitor::assembler() {
  return this->as;
}

const unordered_set<Value>& CompilationVisitor::return_types() const {
  return this->function_return_types;
}

size_t CompilationVisitor::get_file_offset() const {
  return this->file_offset;
}



CompilationVisitor::VariableLocation::VariableLocation() :
    type(ValueType::Indeterminate), global_module(NULL), global_index(-1),
    variable_mem(), variable_mem_valid(false) { }

string CompilationVisitor::VariableLocation::str() const {
  string type_str = this->type.str();
  if (this->global_module) {
    string ret = string_printf("%s.%s (global) = %s @ +%" PRIX64,
        this->global_module->name.c_str(), this->name.c_str(), type_str.c_str(),
        this->global_index);
    if (this->variable_mem_valid) {
      ret += " == ";
      ret += this->variable_mem.str(OperandSize::QuadWord);
    }
    return ret;
  } else {
    string mem_str = this->variable_mem.str(OperandSize::QuadWord);
    return string_printf("%s = %s @ %s", this->name.c_str(), type_str.c_str(),
        mem_str.c_str());
  }
}



Register CompilationVisitor::reserve_register(Register which,
    bool float_register) {
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
    this->as.write_movsd(MemoryReference(rsp, 0),
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

    this->as.write_movsd(MemoryReference(which), MemoryReference(rsp, 0));
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

      this->current_type = Value(ValueType::Bool);
      break;

    case UnaryOperator::Not:
      if ((this->current_type.type == ValueType::Int) ||
          (this->current_type.type == ValueType::Bool)) {
        this->as.write_not(target_mem);
      } else {
        throw compile_error("bitwise not can only be applied to ints and bools", this->file_offset);
      }
      this->current_type = Value(ValueType::Int);
      break;

    case UnaryOperator::Positive:
      // the + operator converts bools into ints; leaves ints and floats alone
      if (this->current_type.type == ValueType::Bool) {
        this->current_type = Value(ValueType::Int);
      } else if ((this->current_type.type != ValueType::Int) &&
                 (this->current_type.type != ValueType::Float)) {
        throw compile_error("arithmetic positive can only be applied to numeric values", this->file_offset);
      }
      break;

    case UnaryOperator::Negative:
      if ((this->current_type.type == ValueType::Bool) ||
          (this->current_type.type == ValueType::Int)) {
        this->as.write_neg(target_mem);
        this->current_type = Value(ValueType::Int);

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

bool CompilationVisitor::is_always_truthy(const Value& type) {
  return (type.type == ValueType::Function) ||
         (type.type == ValueType::Class) ||
         (type.type == ValueType::Module);
}

bool CompilationVisitor::is_always_falsey(const Value& type) {
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
    bool left_holding_reference = this->holding_reference;
    this->write_delete_held_reference(MemoryReference(this->target_register));

    // generate code for the right value
    Value left_type = move(this->current_type);
    try {
      a->right->accept(this);

      if (type_has_refcount(this->current_type.type) && !this->holding_reference) {
        throw compile_error("non-held reference to right binary operator argument",
            this->file_offset);
      }
      if (left_type != this->current_type) {
        throw compile_error("logical combine operator has different return types", this->file_offset);
      }
      if (left_holding_reference != this->holding_reference) {
        throw compile_error("logical combine operator has different reference semantics", this->file_offset);
      }

    } catch (const terminated_by_split&) {
      // we don't know what type right will be, so just use left_type for now
      this->current_type = left_type;
      this->holding_reference = left_holding_reference;
    }
    this->as.write_label(label_name);

    return;
  }

  // all of the remaining operators use both operands, so evaluate both of them
  // into different registers
  // TODO: it's kind of stupid that we push the result onto the stack; figure
  // out a way to implement this without using memory access
  // TODO: delete the held reference to left if right raises
  this->as.write_label(string_printf("__BinaryOperation_%p_evaluate_left", a));
  a->left->accept(this);
  Value left_type = move(this->current_type);
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
  try {
    a->right->accept(this);
  } catch (const terminated_by_split& e) {
    // TODO: delete reference to right if needed
    this->adjust_stack(8);
    throw;
  }
  Value& right_type = this->current_type;
  if (right_type.type == ValueType::Float) {
    this->as.write_movq_from_xmm(target_mem, this->float_target_register);
  }
  this->write_push(this->target_register); // for the destructor call later
  bool right_holding_reference = type_has_refcount(this->current_type.type);
  if (right_holding_reference && !this->holding_reference) {
    throw compile_error("non-held reference to right binary operator argument",
        this->file_offset);
  }

  MemoryReference left_mem(rsp, 8);
  MemoryReference right_mem(rsp, 0);

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

      this->current_type = Value(ValueType::Bool);
      this->holding_reference = false;
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
      this->current_type = Value(ValueType::Bool);
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

      // ints and floats aren't objects, so the `is` operator isn't well-defined
      } else if (left_type.type == ValueType::Int || left_type.type == ValueType::Float) {
        throw compile_error("operator `is` not well-defined for int and float values", this->file_offset);

      // for everything else, just compare the values directly. this exact same
      // code works for bools and all other types, since their values are
      // pointers and we just need to compare the pointers to know if they're
      // the same object
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

      this->current_type = Value(ValueType::Bool);
      this->holding_reference = false;
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
        if (this->available_register(rcx) != rcx) {
          throw compile_error("RCX not available for shift operation", this->file_offset);
        }
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
      if (left_bytes && right_bytes) {
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
        this->current_type = Value(ValueType::Float);

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
        this->current_type = Value(ValueType::Float);

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
        this->current_type = Value(ValueType::Float);

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

      this->current_type = Value(ValueType::Float);
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
          Register r = available_register(rdx);
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
        this->current_type = Value(left_bytes ?
            ValueType::Bytes : ValueType::Unicode);
        this->holding_reference = true;
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
        bool push_rax = (this->target_register != rax) &&
            !this->register_is_available(rax);
        bool push_rdx = (this->target_register != rdx) &&
            !this->register_is_available(rdx);
        if (push_rax) {
          this->write_push(rax);
        }
        if (push_rdx) {
          this->write_push(rdx);
        }

        // TODO: check if right is zero and raise ZeroDivisionError if so

        this->as.write_mov(rax, left_mem);
        this->as.write_xor(rdx, rdx);
        this->as.write_idiv(right_mem);
        if (is_mod) {
          if (this->target_register != rdx) {
            this->as.write_mov(target_mem, rdx);
          }
        } else {
          if (this->target_register != rax) {
            this->as.write_mov(target_mem, rax);
          }
        }

        if (push_rdx) {
          this->write_pop(rdx);
        }
        if (push_rax) {
          this->write_pop(rax);
        }

        // Int // Int == Int
        this->current_type = Value(ValueType::Int);

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
        this->current_type = Value(ValueType::Float);
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
        this->write_raise_exception(this->global->ValueError_class_id,
            L"exponent must be nonnegative");
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
          this->current_type = Value(ValueType::Float);

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
      this->write_delete_reference(MemoryReference(rsp, 8), left_type.type);
    }
    if (type_has_refcount(right_type.type)) {
      this->as.write_label(string_printf("__BinaryOperation_%p_destroy_right", a));
      this->write_delete_reference(MemoryReference(rsp, 16), right_type.type);
    }

    // load the result again and clean up the stack
    this->as.write_mov(MemoryReference(this->target_register),
        MemoryReference(rsp, 0));
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

  int64_t left_callsite_token = -1, right_callsite_token = -1;

  // generate code for the left (True) value
  this->write_delete_held_reference(MemoryReference(this->target_register));
  try {
    a->left->accept(this);
  } catch (const terminated_by_split& e) {
    left_callsite_token = e.callsite_token;
  }
  this->as.write_jmp(end_label);
  Value left_type = this->current_type;
  bool left_holding_reference = this->holding_reference;

  // generate code for the right (False) value
  this->as.write_label(false_label);
  this->write_delete_held_reference(MemoryReference(this->target_register));
  try {
    a->right->accept(this);
  } catch (const terminated_by_split& e) {
    right_callsite_token = e.callsite_token;
  }
  this->as.write_label(end_label);

  // if both of the values were terminated by a split, terminate the entire
  // thing and don't typecheck
  if ((left_callsite_token >= 0) && (right_callsite_token >= 0)) {
    throw terminated_by_split(left_callsite_token);
  }

  // if right was terminated, use the type for left
  if (right_callsite_token >= 0) {
    this->current_type = left_type;
    this->holding_reference = left_holding_reference;

  // if neither was terminated, check that right and left have the same types
  } else if (left_callsite_token < 0) {
    // TODO: support different value types (maybe by splitting the function)
    if (!left_type.types_equal(this->current_type)) {
      string left_s = left_type.str();
      string right_s = this->current_type.str();
      throw compile_error(string_printf("sides have different types (left is %s, right is %s)",
          left_s.c_str(), right_s.c_str()), this->file_offset);
    }
    if (left_holding_reference != this->holding_reference) {
      throw compile_error("sides have different reference semantics", this->file_offset);
    }
  }
}

void CompilationVisitor::visit(ListConstructor* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  this->as.write_label(string_printf("__ListConstructor_%p_setup", a));

  // we'll use rbx to store the list ptr while constructing items and I'm lazy
  if (this->target_register == rbx) {
    throw compile_error("cannot use rbx as target register for list construction", this->file_offset);
  }
  this->write_push(rbx);
  int64_t previously_reserved_registers = this->write_push_reserved_registers();

  // allocate the list object
  this->as.write_label(string_printf("__ListConstructor_%p_allocate", a));
  vector<MemoryReference> int_args({rdi, rsi, r14});
  this->as.write_mov(int_args[0], a->items.size());
  if (type_has_refcount(a->value_type.type)) {
    this->as.write_mov(int_args[1], 1);
  } else {
    this->as.write_xor(int_args[1], int_args[1]);
  }
  this->write_function_call(common_object_reference(void_fn_ptr(&list_new)),
      int_args, {}, -1, this->target_register);

  // save the list items pointer
  this->as.write_mov(rbx, MemoryReference(this->target_register, 0x28));
  this->write_push(rax);

  // generate code for each item and track the extension type
  size_t item_index = 0;
  for (const auto& item : a->items) {
    this->as.write_label(string_printf("__ListConstructor_%p_item_%zu", a, item_index));
    try {
      item->accept(this);
    } catch (const terminated_by_split& e) {
      // TODO: delete unfinished list object
      this->adjust_stack(8);
      this->as.write_pop(rbx);
      throw;
    }

    if (this->current_type.type == ValueType::Float) {
      this->as.write_movsd(MemoryReference(rbx, item_index * 8),
          MemoryReference(this->float_target_register));
    } else {
      this->as.write_mov(MemoryReference(rbx, item_index * 8),
          MemoryReference(this->target_register));
    }
    item_index++;

    // typecheck the value
    if (!a->value_type.types_equal(this->current_type)) {
      throw compile_error("list analysis produced different type than compilation: " +
          a->value_type.type_only().str() + " (analysis) vs " +
          this->current_type.type_only().str() + " (compilation)", this->file_offset);
    }
  }

  // get the list pointer back
  this->as.write_label(string_printf("__ListConstructor_%p_finalize", a));
  this->write_pop(this->target_register);

  // restore the regs we saved
  this->write_pop_reserved_registers(previously_reserved_registers);
  this->write_pop(rbx);

  // the result type is a new reference to a List[extension_type]
  vector<Value> extension_types({a->value_type});
  this->current_type = Value(ValueType::List, extension_types);
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
  // TODO: deduplicate this with ListConstructor

  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  this->as.write_label(string_printf("__TupleConstructor_%p_setup", a));

  // we'll use rbx to store the tuple ptr while constructing items and I'm lazy
  if (this->target_register == rbx) {
    throw compile_error("cannot use rbx as target register for tuple construction", this->file_offset);
  }
  this->write_push(rbx);
  int64_t previously_reserved_registers = this->write_push_reserved_registers();

  // allocate the tuple object
  this->as.write_label(string_printf("__TupleConstructor_%p_allocate", a));
  vector<MemoryReference> int_args({rdi, r14});
  this->as.write_mov(rdi, a->items.size());
  this->write_function_call(common_object_reference(void_fn_ptr(&tuple_new)),
      int_args, {}, -1, this->target_register);

  // save the tuple items pointer
  this->as.write_lea(rbx, MemoryReference(this->target_register, 0x18));
  this->write_push(rax);

  // generate code for each item and track the extension type
  if (a->value_types.size() != a->items.size()) {
    throw compile_error("tuple item count and type count do not match", this->file_offset);
  }
  for (size_t x = 0; x < a->items.size(); x++) {
    const auto& item = a->items[x];
    const auto& expected_type = a->value_types[x];

    this->as.write_label(string_printf("__TupleConstructor_%p_item_%zu", a, x));
    try {
      item->accept(this);
    } catch (const terminated_by_split& e) {
      // TODO: delete unfinished tuple object
      this->adjust_stack(8);
      this->as.write_pop(rbx);
      throw;
    }

    if (this->current_type.type == ValueType::Float) {
      this->as.write_movsd(MemoryReference(rbx, x * 8),
          MemoryReference(this->float_target_register));
    } else {
      this->as.write_mov(MemoryReference(rbx, x * 8),
          MemoryReference(this->target_register));
    }

    if (!expected_type.types_equal(this->current_type)) {
      string analysis_type_str = expected_type.type_only().str();
      string compilation_type_str = this->current_type.type_only().str();
      throw compile_error(string_printf(
          "tuple analysis produced different type than compilation "
          "for item %zu: %s (analysis) vs %s (compilation)", x,
          analysis_type_str.c_str(), compilation_type_str.c_str()), this->file_offset);
    }
  }

  // generate code to write the has_refcount map
  // TODO: we can coalesce these writes for tuples larger than 8 items
  this->as.write_label(string_printf("__TupleConstructor_%p_has_refcount_map", a));
  size_t types_handled = 0;
  while (types_handled < a->value_types.size()) {
    uint8_t value = 0;
    for (size_t x = 0; (x < 8) && ((types_handled + x) < a->value_types.size()); x++) {
      if (type_has_refcount(a->value_types[types_handled + x].type)) {
        value |= (0x80 >> x);
      }
    }
    this->as.write_mov(MemoryReference(rbx,
        a->value_types.size() * 8 + (types_handled / 8)), value, OperandSize::Byte);
    types_handled += 8;
  }

  // get the tuple pointer back
  this->as.write_label(string_printf("__TupleConstructor_%p_finalize", a));
  this->write_pop(this->target_register);

  // restore the regs we saved
  this->write_pop_reserved_registers(previously_reserved_registers);
  this->write_pop(rbx);

  // the result type is a new reference to a Tuple[extension_types]
  this->current_type = Value(ValueType::Tuple, a->value_types);
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
  if (!this->fragment->function ||
      (this->fragment->function->id != a->function_id)) {
    // if the function being declared is a closure, fail
    // TODO: actually implement this check

    // write the function's context to the variable. note that Function-typed
    // variables don't point directly to the code; this is necessary for us to
    // figure out the right fragment at call time
    auto* declared_function_context = this->global->context_for_function(a->function_id);
    this->as.write_mov(this->target_register, reinterpret_cast<int64_t>(declared_function_context));
    this->current_type = Value(ValueType::Function, a->function_id);
    return;
  }

  string base_label = string_printf("LambdaDefinition_%p", a);
  this->write_function_setup(base_label, false);

  this->target_register = rax;
  this->RecursiveASTVisitor::visit(a);
  this->function_return_types.emplace(this->current_type);

  this->write_function_cleanup(base_label, false);
}

struct FunctionCallArgumentValue {
  string name;
  shared_ptr<Expression> passed_value;
  Value default_value; // Indeterminate for positional args
  Value type;

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

  // if the target register is reserved, its value will be preserved instead of
  // being overwritten by the function's return value, which probably isn't what
  // we want
  if (!this->register_is_available(this->target_register)) {
    throw compile_error("target register is reserved at function call time",
        this->file_offset);
  }

  // TODO: if the callee function is unresolved, write a call through the
  // compiler's resolver instead of directly calling it. it will be slow but
  // it will work
  if (a->callee_function_id == 0) {
    throw compile_error("can\'t resolve function reference", this->file_offset);
  }

  // get the function context
  auto* fn = this->global->context_for_function(a->callee_function_id);
  if (!fn) {
    throw compile_error(string_printf("function %" PRId64 " has no context object", a->callee_function_id),
        this->file_offset);
  }

  // if the function is in a different module, we'll need to push r13 and change
  // it to that module's global space pointer before the call. but builtins
  // don't need this because they don't use r13 as the global space pointer
  bool update_global_space_pointer = !fn->is_builtin() && (fn->module != this->module);

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
  // and is the instance object, but only if it was called on an instance
  // object. if it was called on a class object, pass all args directly,
  // including self
  bool add_implicit_self_arg = (fn->class_id && !a->is_class_method_call);
  if (add_implicit_self_arg) {
    arg_values.emplace_back(fn->args[0].name);
    auto& arg = arg_values.back();

    if (fn->is_class_init() != a->is_class_construction) {
      throw compile_error("__init__ may not be called manually", this->file_offset);
    }

    // if the function being called is __init__, allocate a class object first
    // and push it as the first argument (passing an Instance with a NULL
    // pointer instructs the later code to allocate an instance)
    if (a->is_class_construction) {
      arg.default_value = Value(ValueType::Instance, fn->id, nullptr);

    // if it's not __init__, we'll have to know what the instance is; instruct
    // the later code to evaluate the instance pointer instead of the function
    // object
    } else {
      arg.passed_value = a->function;
      arg.evaluate_instance_pointer = true;
    }
    arg.type = Value(ValueType::Instance, fn->class_id, nullptr);
  }

  // push positional args first. if the callee is a method, skip the first
  // positional argument (which will implicitly be self), but only if the
  // function was called as instance.method(...)
  size_t call_arg_index = 0;
  size_t callee_arg_index = add_implicit_self_arg;
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
    arg_values.back().default_value = Value(ValueType::Indeterminate);
  }

  // push remaining args, in the order the function defines them
  for (; callee_arg_index < fn->args.size(); callee_arg_index++) {
    const auto& callee_arg = fn->args[callee_arg_index];

    arg_values.emplace_back(callee_arg.name);
    try {
      const auto& call_arg = keyword_call_args.at(callee_arg.name);
      arg_values.back().passed_value = call_arg;
      arg_values.back().default_value = Value(ValueType::Indeterminate);

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

  // push all reserved registers and r13 if necessary
  int64_t previously_reserved_registers = this->write_push_reserved_registers();
  if (update_global_space_pointer) {
    this->write_push(r13);
  }

  // reserve enough stack space for the worst case - all args are ints (since
  // there are fewer int registers available and floats are less common)
  ssize_t arg_stack_bytes = this->write_function_call_stack_prep(arg_values.size());

  try {
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
          throw compile_error("__init__ call does not have an associated class", this->file_offset);
        }

        this->as.write_label(string_printf("__FunctionCall_%p_evaluate_arg_%zu_alloc_instance",
            a, arg_index));
        this->write_alloc_class_instance(cls->id);

        arg.type = arg.default_value;
        this->current_type = Value(ValueType::Instance, cls->id, NULL);
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
        string s = arg.type.str();
        throw compile_error(string_printf(
            "function call argument %zu (%s) is a non-held reference", arg_index, s.c_str()),
            this->file_offset);
      }

      // store the value on the stack if needed; otherwise, mark the register as
      // reserved
      if (arg.type.type == ValueType::Float) {
        if (float_registers_used != float_argument_register_order.size()) {
          this->reserve_register(this->float_target_register, true);
          float_registers_used++;
        } else {
          this->as.write_movsd(MemoryReference(rsp, stack_offset),
              MemoryReference(this->float_target_register));
          stack_offset += sizeof(double);
        }
      } else {
        if (int_registers_used != int_argument_register_order.size()) {
          this->reserve_register(this->target_register);
          int_registers_used++;
        } else {
          this->as.write_mov(MemoryReference(rsp, stack_offset),
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
    vector<Value> arg_types;
    for (const auto& arg : arg_values) {
      if (arg.is_exception_block) {
        continue; // don't include exc_block in the type signature
      }
      arg_types.emplace_back(arg.type);
    }
    int64_t callee_fragment_index = fn->fragment_index_for_call_args(arg_types);

    string returned_label = string_printf("__FunctionCall_%p_returned", a);

    // if there's no existing fragment and the function isn't builtin, check
    // that the passed argument types match the type annotations
    if ((callee_fragment_index < 0) && !fn->is_builtin()) {
      vector<Value> types_from_annotation;
      for (const auto& arg : fn->args) {
        if (arg.type_annotation.get()) {
          types_from_annotation.emplace_back(this->global->type_for_annotation(
              this->module, arg.type_annotation));
        } else {
          types_from_annotation.emplace_back(ValueType::Indeterminate);
        }
      }
      if (this->global->match_values_to_types(types_from_annotation, arg_types) < 0) {
        throw compile_error("call argument does not match type annotation", this->file_offset);
      }

      // if there's no existing fragment, the function isn't builtin, and eager
      // compilation is enabled, try to compile a new fragment now
      if (!(debug_flags & DebugFlag::NoEagerCompilation)) {
        fn->fragments.emplace_back(fn, fn->fragments.size(), arg_types);
        try {
          compile_fragment(this->global, fn->module, &fn->fragments.back());
          callee_fragment_index = fn->fragments.size() - 1;
        } catch (const compile_error& e) {
          if (debug_flags & DebugFlag::ShowCompileErrors) {
            this->global->print_compile_error(stderr, this->module, e);
          }
          fn->fragments.pop_back();
        }
      }
    }

    // if there's no existing fragment with the right types and this isn't a
    // built-in function, write a call to the compiler instead. if this is a
    // built-in function, fail (there's nothing to recompile)
    if (callee_fragment_index < 0) {
      if (fn->is_builtin()) {
        string args_str;
        for (const auto& v : arg_types) {
          if (!args_str.empty()) {
            args_str += ", ";
          }
          args_str += v.str();
        }
        throw compile_error(string_printf("callee_fragment %s(%s) does not exist",
            fn->name.c_str(), args_str.c_str()), this->file_offset);
      }

      // calling the compiler is a little complicated - we already set up the
      // function call arguments, so we can't just change the call address, and
      // we need a way to tell the compiler what to compile and replace this
      // call with. to deal with this, we put some useful info in r10 and r11
      // before calling it.
      int64_t callsite_token = this->global->next_callsite_token++;
      if (this->fragment->function) {
        this->global->unresolved_callsites.emplace(piecewise_construct,
            forward_as_tuple(callsite_token), forward_as_tuple(
              a->callee_function_id, arg_types, this->module,
              this->fragment->function->id, this->fragment->index, a->split_id));
      } else {
        this->global->unresolved_callsites.emplace(piecewise_construct,
            forward_as_tuple(callsite_token), forward_as_tuple(
              a->callee_function_id, arg_types, this->module, 0, -1, a->split_id));
      }
      if (debug_flags & DebugFlag::ShowJITEvents) {
        string s = this->global->unresolved_callsites.at(callsite_token).str();
        fprintf(stderr, "created unresolved callsite %" PRId64 ": %s\n",
            callsite_token, s.c_str());
      }

      this->as.write_label(string_printf("__FunctionCall_%p_call_compiler_%" PRId64 "_callsite_%" PRId64,
          a, a->callee_function_id, callsite_token));
      this->as.write_mov(r10, reinterpret_cast<int64_t>(this->global));
      this->as.write_mov(r11, callsite_token);

      // if this call ever returns to this point in the code, it must raise an
      // exception, so just go directly to the exception handler if it does.
      this->as.write_push(common_object_reference(void_fn_ptr(&_unwind_exception_internal)));
      this->as.write_jmp(common_object_reference(void_fn_ptr(&_resolve_function_call)));

      // we're done here - can't compile any more since we don't know the return
      // type of this function. but we have to compile the rest of the scope to
      // get the exception handlers
      this->current_type = Value(ValueType::Indeterminate);
      this->holding_reference = false;
      throw terminated_by_split(callsite_token);
    }

    // the fragment exists, so we can call it
    const auto& callee_fragment = fn->fragments[callee_fragment_index];

    string call_split_label = string_printf("__FunctionCall_%p_call_function_%" PRId64 "_fragment_%" PRId64 "_split_%" PRId64,
        a, a->callee_function_id, callee_fragment_index, a->split_id);
    this->as.write_label(call_split_label);
    this->fragment->call_split_labels.at(a->split_id) = call_split_label;

    // call the fragment. note that the stack is already properly aligned here
    if (update_global_space_pointer) {
      this->as.write_mov(r13, reinterpret_cast<int64_t>(fn->module->global_space));
    }
    this->as.write_mov(rax, reinterpret_cast<int64_t>(callee_fragment.compiled));
    this->as.write_call(rax);
    this->as.write_label(returned_label);

    // if the function raised an exception, the return value is meaningless;
    // instead we should continue unwinding the stack
    string no_exc_label = string_printf("__FunctionCall_%p_no_exception", a);
    this->as.write_test(r15, r15);
    this->as.write_jz(no_exc_label);
    this->as.write_jmp(common_object_reference(void_fn_ptr(&_unwind_exception_internal)));
    this->as.write_label(no_exc_label);

    // put the return value into the target register
    if (callee_fragment.return_type.type == ValueType::Float) {
      if (this->target_register != rax) {
        this->as.write_label(string_printf("__FunctionCall_%p_save_return_value", a));
        this->as.write_movsd(MemoryReference(this->float_target_register), xmm0);
      }
    } else {
      if (this->target_register != rax) {
        this->as.write_label(string_printf("__FunctionCall_%p_save_return_value", a));
        this->as.write_mov(MemoryReference(this->target_register), rax);
      }
    }

    // functions always return new references, unless they return trivial types
    this->current_type = callee_fragment.return_type;
    this->holding_reference = type_has_refcount(this->current_type.type);

    // note: we don't have to destroy the function arguments; we passed the
    // references that we generated directly into the function and it's
    // responsible for deleting those references

  } catch (const terminated_by_split&) {
    this->as.write_label(string_printf("__FunctionCall_%p_restore_stack", a));
    this->adjust_stack(arg_stack_bytes);
    if (update_global_space_pointer) {
      this->write_pop(r13);
    }
    this->write_pop_reserved_registers(previously_reserved_registers);
    throw;
  }

  // unreserve the argument stack space
  this->as.write_label(string_printf("__FunctionCall_%p_restore_stack", a));
  this->adjust_stack(arg_stack_bytes);
  if (update_global_space_pointer) {
    this->write_pop(r13);
  }
  this->write_pop_reserved_registers(previously_reserved_registers);
}

void CompilationVisitor::visit(ArrayIndex* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  // TODO: this leakes a reference! need to delete the reference to the
  // collection (and key if it's a dict). maybe can fix this by using reference-
  // absorbing functions instead?

  // get the collection
  a->array->accept(this);
  Value collection_type = move(this->current_type);
  if (!this->holding_reference) {
    throw compile_error("not holding reference to collection", this->file_offset);
  }

  // save regs (all cases below need to evaluate more expressions)
  Register original_target_register = this->target_register;
  int64_t previously_reserved_registers = this->write_push_reserved_registers();

  try {
    // I'm lazy and don't want to duplicate work - just call the appropriate
    // get_item function

    if (collection_type.type == ValueType::Dict) {

      // arg 1 is the dict object
      if (this->target_register != rdi) {
        this->as.write_mov(rdi, MemoryReference(this->target_register));
      }

      // compute the key
      this->target_register = rsi;
      this->reserve_register(rdi);
      a->index->accept(this);
      if (!this->current_type.types_equal(collection_type.extension_types[0])) {
        string expr_key_type = this->current_type.str();
        string dict_key_type = collection_type.extension_types[0].str();
        string dict_value_type = collection_type.extension_types[1].str();
        throw compile_error(string_printf("lookup for key of type %s on Dict[%s, %s]",
            expr_key_type.c_str(), dict_key_type.c_str(), dict_value_type.c_str()),
            this->file_offset);
      }
      if (type_has_refcount(this->current_type.type) && !this->holding_reference) {
        throw compile_error("not holding reference to key", this->file_offset);
      }
      this->release_register(rdi);

      // get the dict item
      this->write_function_call(common_object_reference(void_fn_ptr(&dictionary_at)),
          {rdi, rsi, r14}, {}, -1, original_target_register);

      // the return type is the value extension type
      this->current_type = collection_type.extension_types[1];

    } else if ((collection_type.type == ValueType::List) ||
               (collection_type.type == ValueType::Tuple)) {

      // arg 1 is the list/tuple object
      if (this->target_register != rdi) {
        this->as.write_mov(rdi, MemoryReference(this->target_register));
      }

      // for lists, the index can be dynamic; compute it now
      int64_t tuple_index = a->index_value;
      if (collection_type.type == ValueType::List) {
        this->target_register = rsi;
        this->reserve_register(rdi);
        a->index->accept(this);
        if (this->current_type.type != ValueType::Int) {
          throw compile_error("list index must be Int; here it\'s " + this->current_type.str(),
              this->file_offset);
        }
        this->release_register(rdi);

      // for tuples, the index must be static since the result type depends on it.
      // for this reason, it also needs to be in range of the extension types
      } else {
        if (!a->index_constant) {
          throw compile_error("tuple indexes must be constants", this->file_offset);
        }
        if (tuple_index < 0) {
          tuple_index += collection_type.extension_types.size();
        }
        if ((tuple_index < 0) || (tuple_index >= static_cast<ssize_t>(
            collection_type.extension_types.size()))) {
          throw compile_error("tuple index out of range", this->file_offset);
        }
        this->as.write_mov(rsi, tuple_index);
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

    } else {
      // TODO
      throw compile_error("ArrayIndex not yet implemented for collections of type " + collection_type.str(),
          this->file_offset);
    }

    // if the return value has a refcount, then the function returned a new
    // reference to it
    this->holding_reference = type_has_refcount(this->current_type.type);

    // if the return value is a float, it's currently in an int register; move
    // it to an xmm reg if needed
    if (this->current_type.type == ValueType::Float) {
      this->as.write_movq_to_xmm(this->float_target_register,
          MemoryReference(this->target_register));
    }

  } catch (const terminated_by_split&) {
    this->write_pop_reserved_registers(previously_reserved_registers);
    this->target_register = original_target_register;
    throw;
  }

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
  this->current_type = Value(ValueType::Int);
  this->holding_reference = false;
}

void CompilationVisitor::visit(FloatConstant* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  this->write_load_double(this->float_target_register, a->value);
  this->current_type = Value(ValueType::Float);
  this->holding_reference = false;
}

void CompilationVisitor::visit(BytesConstant* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  const BytesObject* o = this->global->get_or_create_constant(a->value);
  this->as.write_mov(this->target_register, reinterpret_cast<int64_t>(o));
  this->write_add_reference(this->target_register);

  this->current_type = Value(ValueType::Bytes);
  this->holding_reference = true;
}

void CompilationVisitor::visit(UnicodeConstant* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  const UnicodeObject* o = this->global->get_or_create_constant(a->value);
  this->as.write_mov(this->target_register, reinterpret_cast<int64_t>(o));
  this->write_add_reference(this->target_register);

  this->current_type = Value(ValueType::Unicode);
  this->holding_reference = true;
}

void CompilationVisitor::visit(TrueConstant* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  this->as.write_mov(this->target_register, 1);
  this->current_type = Value(ValueType::Bool);
  this->holding_reference = false;
}

void CompilationVisitor::visit(FalseConstant* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  MemoryReference target_mem(this->target_register);
  this->as.write_xor(target_mem, target_mem);
  this->current_type = Value(ValueType::Bool);
  this->holding_reference = false;
}

void CompilationVisitor::visit(NoneConstant* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  MemoryReference target_mem(this->target_register);
  this->as.write_xor(target_mem, target_mem);
  this->current_type = Value(ValueType::None);
  this->holding_reference = false;
}

void CompilationVisitor::visit(VariableLookup* a) {
  this->file_offset = a->file_offset;
  this->assert_not_evaluating_instance_pointer();

  VariableLocation loc = this->location_for_variable(a->name);
  this->write_read_variable(this->target_register, this->float_target_register, loc);

  this->current_type = loc.type;
  this->holding_reference = type_has_refcount(loc.type.type);
  if (this->current_type.type == ValueType::Indeterminate) {
    throw compile_error("variable has Indeterminate type: " + loc.str(), this->file_offset);
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
    // so we require Imported phase here to enforce this ordering later.
    auto base_module = this->global->get_or_create_module(a->base_module_name);
    advance_module_phase(this->global, base_module.get(), ModuleContext::Phase::Imported);

    VariableLocation loc = this->location_for_global(base_module.get(), a->name);
    this->write_read_variable(this->target_register, this->float_target_register, loc);

    this->current_type = loc.type;
    this->holding_reference = type_has_refcount(loc.type.type);
    if (this->current_type.type == ValueType::Indeterminate) {
      throw compile_error("attribute has Indeterminate type", this->file_offset);
    }

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
  bool base_holding_reference = this->holding_reference;

  // if the base object is a class, write code that gets the attribute
  if (this->current_type.type == ValueType::Instance) {
    auto* cls = this->global->context_for_class(this->current_type.class_id);
    VariableLocation loc = this->location_for_attribute(cls, a->name, this->target_register);

    // get the attribute value. note that we reserve the base reg in case adding
    // a reference to the attr causes a function call (we need the base later)
    this->as.write_label(string_printf("__AttributeLookup_%p_get_value", a));
    this->reserve_register(base_register);
    this->write_read_variable(attr_register, this->float_target_register, loc);
    this->release_register(base_register);

    // if we're holding a reference to the base, delete that reference now
    if (base_holding_reference) {
      this->reserve_register(attr_register);
      this->write_delete_held_reference(MemoryReference(base_register));
      this->release_register(attr_register);
    }

    this->target_register = attr_register;
    this->current_type = loc.type;
    this->holding_reference = type_has_refcount(this->current_type.type);
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
    Value value_type = move(this->current_type);

    // evaluate the base object
    a->base->accept(this);

    if (this->current_type.type == ValueType::Instance) {
      // the class should have the attribute that we're setting
      // (location_for_attribute will check this) and the attr should be the
      // same type as the value being written
      auto* cls = this->global->context_for_class(this->current_type.class_id);
      if (!cls) {
        throw compile_error("object class does not exist", this->file_offset);
      }
      VariableLocation loc = this->location_for_attribute(cls, a->name,
          this->target_register);
      size_t attr_index = cls->attribute_indexes.at(a->name);
      const auto& attr = cls->attributes.at(attr_index);
      if (!attr.value.types_equal(loc.type)) {
        string attr_type = attr.value.str();
        string new_type = value_type.str();
        throw compile_error(string_printf("attribute %s changes type from %s to %s",
            a->name.c_str(), attr_type.c_str(), new_type.c_str()),
            this->file_offset);
      }

      // reserve the base in case delete_reference needs to be called
      this->reserve_register(this->target_register);
      this->write_write_variable(value_register, this->float_target_register, loc);
      this->release_register(this->target_register);

      // if we're holding a reference to the base, delete it
      this->write_delete_held_reference(MemoryReference(this->target_register));

      // clean up
      this->target_register = value_register;
      this->release_register(this->target_register);

    } else if (this->current_type.type == ValueType::Module) {
      // the class should have the attribute that we're setting
      // (location_for_attribute will check this) and the attr should be the
      // same type as the value being written
      if (!this->current_type.value_known) {
        throw compile_error("base is module, but value is unknown", this->file_offset);
      }
      auto base_module = this->global->get_or_create_module(*this->current_type.bytes_value);

      // note: we use Imported here to make sure the target module's root scope
      // runs before any external code that could modify it
      advance_module_phase(this->global, base_module.get(), ModuleContext::Phase::Imported);

      VariableLocation loc = this->location_for_global(base_module.get(), a->name);
      if (loc.variable_mem_valid) {
        throw compile_error("variable reference should not be valid", this->file_offset);
      }

      this->reserve_register(this->target_register);
      this->write_write_variable(value_register, this->float_target_register, loc);
      this->release_register(this->target_register);

    } else {
      string s = this->current_type.str();
      throw compile_error(string_printf("cannot dynamically set attribute on %s", s.c_str()),
          this->file_offset);
    }

  // if a->base is missing, then it's a simple variable write
  } else {
    VariableLocation loc = this->location_for_variable(a->name);

    // typecheck the result. the type of a variable can only be changed if it's
    // Indeterminate; otherwise it's an error
    // TODO: deduplicate some of this with location_for_variable
    Value* target_variable = NULL;
    if (loc.global_module) {
      target_variable = &loc.global_module->global_variables.at(loc.name).value;
    } else {
      target_variable = &this->local_variable_types.at(loc.name);
    }
    if (!target_variable) {
      throw compile_error("target variable not found", this->file_offset);
    }
    if (target_variable->type == ValueType::Indeterminate) {
      *target_variable = this->current_type;
    } else if (!target_variable->types_equal(loc.type)) {
      string target_type_str = target_variable->str();
      string value_type_str = current_type.str();
      throw compile_error(string_printf("variable %s changes type from %s to %s\n",
          loc.name.c_str(), target_type_str.c_str(),
          value_type_str.c_str()), this->file_offset);
    }

    // loc may be Indeterminate; for example, if the only assignment to a
    // variable is the result of a function call. it always makes sense to use
    // the target variable type here, and we checked that they match above if
    // we have enough information to do so
    loc.type = *target_variable;

    this->reserve_register(this->target_register);
    this->write_write_variable(this->target_register, this->float_target_register, loc);
    this->release_register(this->target_register);
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
  this->write_push(rbp);
  this->as.write_mov(rbp, rsp);
  this->write_push(r12);
  this->as.write_mov(r12, reinterpret_cast<int64_t>(common_object_base()));
  this->write_push(r13);
  this->as.write_mov(r13, reinterpret_cast<int64_t>(this->module->global_space));
  this->write_push(r14);
  this->as.write_xor(r14, r14);
  this->write_push(r15);
  this->as.write_xor(r15, r15);

  // create an exception block for the root scope
  // this exception block just returns from the module scope - but the calling
  // code checks for a nonzero return value (which means an exception is active)
  // and will handle it appropriately
  string exc_label = string_printf("__ModuleStatement_%p_exc", a);
  this->as.write_label(string_printf("__ModuleStatement_%p_create_exc_block", a));
  this->write_create_exception_block({}, exc_label);

  // generate the function's code
  this->target_register = rax;
  this->as.write_label(string_printf("__ModuleStatement_%p_body", a));
  try {
    this->visit_list(a->items);
  } catch (const terminated_by_split&) { }

  // we're done; write the cleanup
  this->as.write_label(string_printf("__ModuleStatement_%p_return", a));
  this->adjust_stack(return_exception_block_size);
  this->as.write_label(exc_label);
  this->as.write_mov(rax, r15);
  this->write_pop(r15);
  this->write_pop(r14);
  this->write_pop(r13);
  this->write_pop(r12);
  this->write_pop(rbp);

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
    throw compile_error("import * is not supported", this->file_offset);
  }

  // case 1: import entire modules, not specific names
  if (a->names.empty()) {
    // we actually don't need to do anything here; module lookups are always
    // done statically
    return;
  }

  // case 2: import some names from a module (from x import y)
  const string& base_module_name = a->modules.begin()->first;
  auto base_module = this->global->get_or_create_module(base_module_name);
  advance_module_phase(this->global, base_module.get(), ModuleContext::Phase::Imported);
  MemoryReference target_mem(this->target_register);
  for (const auto& it : a->names) {
    VariableLocation src_loc = this->location_for_global(base_module.get(), it.first);
    VariableLocation dest_loc = this->location_for_variable(it.second);

    this->as.write_label(string_printf("__ImportStatement_%p_copy_%s_%s",
        a, it.first.c_str(), it.second.c_str()));

    // get the value from the other module
    this->as.write_mov(target_mem, reinterpret_cast<int64_t>(src_loc.global_module->global_space));
    this->as.write_mov(target_mem, MemoryReference(this->target_register, sizeof(int64_t) * src_loc.global_index));

    // if it's an object, add a reference to it
    if (type_has_refcount(src_loc.type.type)) {
      this->write_add_reference(this->target_register);
    }

    // store the value in this module. variable_mem is valid if global_module is
    // this module or NULL
    if (!dest_loc.variable_mem_valid) {
      throw compile_error("variable reference not valid", a->file_offset);
    }
    this->as.write_mov(dest_loc.variable_mem, target_mem);
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
  Value truth_value_type = this->current_type;
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
  // TODO: deduplicate most of the rest of this function (below here) with
  // write_raise_exception
  auto* cls = this->global->context_for_class(this->global->AssertionError_class_id);
  if (!cls) {
    throw compile_error("AssertionError class does not exist",
        this->file_offset);
  }

  // allocate the AssertionError instance and put the message in it
  this->as.write_label(string_printf("__AssertStatement_%p_allocate_instance", a));
  this->write_alloc_class_instance(this->global->AssertionError_class_id, false);
  Register tmp = this->available_register_except({this->target_register});
  this->write_pop(tmp);

  // fill in the attributes as needed
  const auto* cls_init = this->global->context_for_function(this->global->AssertionError_class_id);
  size_t message_index = cls->attribute_indexes.at("message");
  size_t init_index = cls->attribute_indexes.at("__init__");
  size_t message_offset = cls->offset_for_attribute(message_index);
  size_t init_offset = cls->offset_for_attribute(init_index);
  this->as.write_mov(MemoryReference(this->target_register, message_offset),
      MemoryReference(tmp));
  this->as.write_mov(tmp, reinterpret_cast<int64_t>(cls_init));
  this->as.write_mov(MemoryReference(this->target_register, init_offset),
      MemoryReference(tmp));

  // we should have filled everything in
  if (cls->instance_size() != 40) {
    throw compile_error("did not fill in entire AssertionError structure",
        this->file_offset);
  }

  // now jump to unwind_exception
  this->as.write_label(string_printf("__AssertStatement_%p_unwind", a));
  this->as.write_mov(r15, MemoryReference(this->target_register));
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

  if (!this->fragment->function) {
    throw compile_error("return statement outside function definition", this->file_offset);
  }

  // the value should be returned in rax
  this->as.write_label(string_printf("__ReturnStatement_%p_evaluate_expression", a));
  this->target_register = rax;
  try {
    a->value->accept(this);
  } catch (const terminated_by_split&) {
    this->function_return_types.emplace(ValueType::Indeterminate);
    throw;
  }

  // it had better be a new reference if the type is nontrivial
  if (type_has_refcount(this->current_type.type) && !this->holding_reference) {
    throw compile_error("can\'t return reference to " + this->current_type.str(),
        this->file_offset);
  }

  // if the function has a type annotation, enforce that the return type matches
  const Value& annotated_return_type = this->fragment->function->annotated_return_type;
  if ((annotated_return_type.type != ValueType::Indeterminate) &&
      (this->global->match_value_to_type(annotated_return_type, this->current_type) < 0)) {
    throw compile_error("returned value does not match type annotation", this->file_offset);
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
    try {
      this->visit_list(a->items);
    } catch (const terminated_by_split&) { }
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
    try {
      this->visit_list(a->items);
    } catch (const terminated_by_split&) { }
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
  try {
    this->visit_list(a->items);
  } catch (const terminated_by_split&) { }
}

void CompilationVisitor::visit(ElifStatement* a) {
  this->file_offset = a->file_offset;

  // just do the sub-statements; the encapsulating logic is all in IfStatement
  this->as.write_label(string_printf("__ElifStatement_%p", a));
  try {
    this->visit_list(a->items);
  } catch (const terminated_by_split&) { }
}

void CompilationVisitor::visit(ForStatement* a) {
  this->file_offset = a->file_offset;

  // get the collection object and save it on the stack
  this->as.write_label(string_printf("__ForStatement_%p_get_collection", a));
  a->collection->accept(this);
  Value collection_type = this->current_type;
  this->write_push(this->target_register);

  // we'll use rbx for some loop state (e.g. the item index in lists)
  if (this->target_register == rbx) {
    throw compile_error("cannot use rbx as target register for list iteration", this->file_offset);
  }
  this->write_push(rbx);
  this->as.write_xor(rbx, rbx);

  try {
    string next_label = string_printf("__ForStatement_%p_next", a);
    string end_label = string_printf("__ForStatement_%p_complete", a);
    string break_label = string_printf("__ForStatement_%p_broken", a);

    if ((collection_type.type == ValueType::List) ||
        (collection_type.type == ValueType::Tuple)) {

      // tuples containing disparate types can't be iterated
      if ((collection_type.type == ValueType::Tuple) &&
          (!collection_type.extension_types.empty())) {
        Value uniform_extension_type = collection_type.extension_types[0];
        for (const Value& extension_type : collection_type.extension_types) {
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
          MemoryReference(rsp, 8));

      // check if we're at the end and skip the body if so
      this->as.write_cmp(rbx, MemoryReference(this->target_register, 0x10));
      this->as.write_jge(end_label);

      // get the next item
      if (collection_type.type == ValueType::List) {
        this->as.write_mov(MemoryReference(this->target_register),
            MemoryReference(this->target_register, 0x28));
        if (item_type == ValueType::Float) {
          this->as.write_movq_to_xmm(this->float_target_register,
              MemoryReference(this->target_register, 0, rbx, 8));
        } else {
          this->as.write_mov(MemoryReference(this->target_register),
              MemoryReference(this->target_register, 0, rbx, 8));
        }
      } else {
        if (item_type == ValueType::Float) {
          this->as.write_movq_to_xmm(this->float_target_register,
              MemoryReference(this->target_register, 0x18, rbx, 8));
        } else {
          this->as.write_mov(MemoryReference(this->target_register),
              MemoryReference(this->target_register, 0x18, rbx, 8));
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
      try {
        this->visit_list(a->items);
      } catch (const terminated_by_split&) {
        this->continue_label_stack.pop_back();
        this->break_label_stack.pop_back();
        throw;
      }
      this->continue_label_stack.pop_back();
      this->break_label_stack.pop_back();
      this->as.write_jmp(next_label);
      this->as.write_label(end_label);

    } else if (collection_type.type == ValueType::Dict) {

      int64_t previously_reserved_registers = this->write_push_reserved_registers();

      // create a SlotContents structure
      // TODO: figure out how this structure interacts with refcounting and
      // exceptions (or if it does at all)
      this->adjust_stack(-sizeof(DictionaryObject::SlotContents));
      try {
        this->as.write_mov(MemoryReference(rsp, 0), 0);
        this->as.write_mov(MemoryReference(rsp, 8), 0);
        this->as.write_mov(MemoryReference(rsp, 16), 0);

        // get the dict object and SlotContents pointer. we +8 to the offset
        // because we saved rbx between the SlotContents struct and the
        // collection pointer
        this->as.write_label(next_label);
        this->as.write_mov(rdi, MemoryReference(rsp,
            sizeof(DictionaryObject::SlotContents) + 8));
        this->write_add_reference(rdi);
        this->as.write_mov(rdi, MemoryReference(rsp,
            sizeof(DictionaryObject::SlotContents) + 8));
        this->as.write_mov(rsi, rsp);

        // call dictionary_next_item
        this->write_function_call(
            common_object_reference(void_fn_ptr(&dictionary_next_item)), {rdi, rsi}, {});

        // if dictionary_next_item returned 0, then we're done
        this->as.write_test(rax, rax);
        this->as.write_je(end_label);

        // get the key pointer
        this->as.write_mov(MemoryReference(this->target_register),
            MemoryReference(rsp, 0));

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
        try {
          this->visit_list(a->items);
        } catch (const terminated_by_split&) {
          this->continue_label_stack.pop_back();
          this->break_label_stack.pop_back();
          throw;
        }
        this->continue_label_stack.pop_back();
        this->break_label_stack.pop_back();
        this->as.write_jmp(next_label);
        this->as.write_label(end_label);

      } catch (const terminated_by_split&) {
        this->adjust_stack(sizeof(DictionaryObject::SlotContents));
        this->write_pop_reserved_registers(previously_reserved_registers);
        throw;
      }
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

  } catch (const terminated_by_split&) {
    // note: all collection types have refcounts, so we don't check the type of
    // target_register here
    this->write_pop(rbx);
    this->write_pop(this->target_register);
    this->write_delete_reference(MemoryReference(this->target_register),
        collection_type.type);
    throw;
  }

  this->write_pop(rbx);
  this->write_pop(this->target_register);
  this->write_delete_reference(MemoryReference(this->target_register),
      collection_type.type);
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
  try {
    this->visit_list(a->items);
  } catch (const terminated_by_split&) { }
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
  try {
    this->visit_list(a->items);
  } catch (const terminated_by_split&) { }
}

void CompilationVisitor::visit(FinallyStatement* a) {
  this->file_offset = a->file_offset;

  bool prev_in_finally_block = this->in_finally_block;
  this->in_finally_block = true;

  // we save the active exception and clear it, so the finally block can contain
  // further try/except blocks without clobbering the active exception
  this->as.write_label(string_printf("__FinallyStatement_%p_save_exc", a));
  this->write_push(r15);
  this->as.write_xor(r15, r15);

  this->as.write_label(string_printf("__FinallyStatement_%p_body", a));
  try {
    this->visit_list(a->items);
  } catch (const terminated_by_split&) { }

  // if there's now an active exception, then the finally block raised an
  // exception of its own. if there was a saved exception object, destroy it;
  // then start unwinding the new exception
  string no_exc_label = string_printf("__FinallyStatement_%p_no_exc", a);
  string end_label = string_printf("__FinallyStatement_%p_end", a);
  this->as.write_label(string_printf("__FinallyStatement_%p_restore_exc", a));
  this->as.write_test(r15, r15);
  this->as.write_jz(no_exc_label);
  this->write_delete_reference(MemoryReference(r15, 0), ValueType::Instance);
  this->as.write_jmp(common_object_reference(void_fn_ptr(&_unwind_exception_internal)));

  // the finally block did not raise an exception, but there may be a saved
  // exception. if so, unwind it now
  this->as.write_label(no_exc_label);
  this->write_pop(r15);
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

  // generate the try block body. we catch terminated_by_split here because
  // calling a split can cause an exception to be raised, and the code might
  // need to catch it
  try {
    this->as.write_label(string_printf("__TryStatement_%p_body", a));
    this->visit_list(a->items);
  } catch (const terminated_by_split& e) { }

  // remove the exception block from the stack
  // the previous exception block pointer is the first field in the exception
  // block, so load r14 from there
  this->as.write_label(string_printf("__TryStatement_%p_remove_exc_blocks", a));
  this->as.write_mov(r14, MemoryReference(rsp, 0));
  this->adjust_stack_to(stack_bytes_used_on_restore);

  // generate the else block if there is one. the exc block is already gone, so
  // this code isn't covered by the except clauses, but we need to generate a
  // new exc block to make this be covered by the finally clause
  if (a->else_suite.get()) {
    this->as.write_label(string_printf("__TryStatement_%p_create_else_exc_block", a));
    this->write_create_exception_block({}, finally_label);
    try {
      a->else_suite->accept(this);
    } catch (const terminated_by_split&) {
      this->as.write_label(string_printf("__TryStatement_%p_delete_else_exc_block", a));
      this->as.write_mov(r14, MemoryReference(rsp, 0));
      this->adjust_stack_to(stack_bytes_used_on_restore);
      throw;
    }
    this->as.write_label(string_printf("__TryStatement_%p_delete_else_exc_block", a));
    this->as.write_mov(r14, MemoryReference(rsp, 0));
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
      Value* target_variable = NULL;
      if (loc.global_module) {
        if (!loc.variable_mem_valid) {
          throw compile_error("exception reference not valid", a->file_offset);
        }
        target_variable = &loc.global_module->global_variables.at(loc.name).value;
      } else {
        target_variable = &this->local_variable_types.at(loc.name);
      }
      if (!target_variable) {
        throw compile_error("target variable not found in exception block", a->file_offset);
      }

      if ((target_variable->type != ValueType::Instance) ||
          !except->class_ids.count(target_variable->class_id)) {
        throw compile_error(string_printf("variable %s is not an exception instance type",
            loc.name.c_str()), a->file_offset);
      }

      // delete the old value if present, save the new value, and clear the active
      // exception pointer
      this->write_delete_reference(loc.variable_mem, loc.type.type);
      this->as.write_mov(loc.variable_mem, r15);
    }

    // clear the active exception
    this->as.write_xor(r15, r15);

    // generate the except block code
    this->as.write_label(string_printf("__TryStatement_%p_except_%zd_body",
        a, except_index));
    try {
      except->accept(this);
    } catch (const terminated_by_split&) { }

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
    try {
      a->finally_suite->accept(this);
    } catch (const terminated_by_split&) { }
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

  string base_label = string_printf("FunctionDefinition_%p_%s", a, a->name.c_str());

  // if this definition is not the function being compiled, don't recur; instead
  // treat it as an assignment (of the function context to the local/global var)
  if (!this->fragment->function ||
      (this->fragment->function->id != a->function_id)) {

    // if the function being declared is a closure, fail
    // TODO: actually implement this check

    // write the function's context to the variable. note that Function-typed
    // variables don't point directly to the code; this is necessary for us to
    // figure out the right fragment at call time
    auto* declared_function_context = this->global->context_for_function(a->function_id);
    auto loc = this->location_for_variable(a->name);
    if (!loc.variable_mem_valid) {
      throw compile_error("function definition reference not valid", this->file_offset);
    }
    this->as.write_label("__" + base_label);
    this->as.write_mov(this->target_register, reinterpret_cast<int64_t>(declared_function_context));
    this->as.write_mov(loc.variable_mem, MemoryReference(this->target_register));
    return;
  }

  // if the function being compiled is __del__ on a class, we need to set up the
  // special registers within the function, since it can be called from anywhere
  // (even non-nemesys code)
  bool setup_special_regs = (this->fragment->function->class_id) &&
      (this->fragment->function->name == "__del__");

  this->write_function_setup(base_label, setup_special_regs);
  this->target_register = rax;
  try {
    this->visit_list(a->decorators);
    for (auto& arg : a->args.args) {
      if (arg.default_value.get()) {
        arg.default_value->accept(this);
      }
    }
    this->visit_list(a->items);

  } catch (const terminated_by_split&) {
    this->write_function_cleanup(base_label, setup_special_regs);
    throw;
  }

  // if the function is __init__, implicitly return self (the function cannot
  // explicitly return a value)
  if (this->fragment->function->is_class_init()) {
    // the value should be returned in rax
    this->as.write_label(string_printf("__FunctionDefinition_%p_return_self_from_init", a));

    VariableLocation loc = this->location_for_variable("self");
    if (!loc.variable_mem_valid) {
      throw compile_error("self reference not valid", this->file_offset);
    }
    if (!type_has_refcount(loc.type.type)) {
      throw compile_error("self is not an object", this->file_offset);
    }

    // add a reference to self and load it into rax for returning
    // TODO: should we set holding_reference to true here?
    this->target_register = rax;
    this->as.write_mov(MemoryReference(this->target_register), loc.variable_mem);
    this->write_add_reference(this->target_register);
  }

  this->write_function_cleanup(base_label, setup_special_regs);
}

void CompilationVisitor::visit(ClassDefinition* a) {
  this->file_offset = a->file_offset;

  // write the class' context to the variable
  auto loc = this->location_for_variable(a->name);
  if (!loc.variable_mem_valid) {
    throw compile_error("self reference not valid", this->file_offset);
  }

  this->as.write_label(string_printf("__ClassDefinition_%p_assign", a));
  auto* cls = this->global->context_for_class(a->class_id);
  this->as.write_mov(this->target_register, reinterpret_cast<int64_t>(cls));
  this->as.write_mov(loc.variable_mem, MemoryReference(this->target_register));

  // create the class destructor function
  if (!cls->destructor) {
    // if none of the class attributes have destructors and it doesn't have a
    // __del__ method, then the overall class destructor trivializes to free()
    ssize_t del_index = -1;
    try {
      del_index = cls->attribute_indexes.at("__del__");
    } catch (const out_of_range&) { }
    bool has_subdestructors = (del_index >= 0);
    if (!has_subdestructors) {
      for (const auto& it : cls->attributes) {
        if (type_has_refcount(it.value.type)) {
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
      dtor_as.write_push(rbp);
      dtor_as.write_mov(rbp, rsp);

      // we'll keep the object pointer in rbx since it's callee-save
      dtor_as.write_push(rbx);
      dtor_as.write_mov(rbx, rdi);

      // align the stack
      dtor_as.write_sub(rsp, 8);

      // we have to add a fake reference to the object while destroying it;
      // otherwise __del__ will call this destructor recursively
      dtor_as.write_lock();
      dtor_as.write_inc(MemoryReference(rbx, 0));

      // call __del__ before deleting attribute references
      if (del_index >= 0) {
        // figure out what __del__ actually is
        auto& del_attr = cls->attributes.at(del_index);
        if (del_attr.value.type != ValueType::Function) {
          throw compile_error("__del__ exists but is not a function; instead it\'s " + del_attr.value.str(), this->file_offset);
        }
        if (!del_attr.value.value_known) {
          throw compile_error("__del__ exists but is an unknown value", this->file_offset);
        }

        auto* fn = this->global->context_for_function(del_attr.value.function_id);

        // get or generate the Fragment object. this function should have at
        // most one fragment because __del__ cannot take arguments
        if (fn->fragments.size() > 1) {
          throw compile_error("__del__ has multiple fragments", this->file_offset);
        }
        vector<Value> expected_arg_types({Value(ValueType::Instance, a->class_id, NULL)});
        if (fn->fragments.empty()) {
          vector<Value> arg_types({Value(ValueType::Instance, a->class_id, NULL)});
          fn->fragments.emplace_back(fn, fn->fragments.size(), arg_types);
          compile_fragment(this->global, fn->module, &fn->fragments.back());
        }
        auto fragment = fn->fragments.back();
        if (fragment.arg_types != expected_arg_types) {
          throw compile_error("__del__ fragment takes incorrect argument types", this->file_offset);
        }

        // generate the call to the fragment. note that the instance pointer is
        // still in rdi, so we don't have to do anything to prepare
        dtor_as.write_lock();
        dtor_as.write_inc(MemoryReference(rbx, 0)); // reference for the function arg
        dtor_as.write_mov(rax, reinterpret_cast<int64_t>(fragment.compiled));
        dtor_as.write_call(rax);

        // __del__ can add new references to the object; if this happens, don't
        // proceed with the destruction
        // TODO: if the refcount is zero, something has gone seriously wrong.
        // what should we do in that case?
        // TODO: do we need the lock prefix to do this compare?
        dtor_as.write_cmp(MemoryReference(rbx, 0), 1);
        dtor_as.write_je(base_label + "_proceed");
        dtor_as.write_lock();
        dtor_as.write_dec(MemoryReference(rbx, 0)); // fake reference
        dtor_as.write_add(rsp, 8);
        dtor_as.write_pop(rbx);
        dtor_as.write_ret();
        dtor_as.write_label(base_label + "_proceed");
      }

      // the first 2 fields are the refcount and destructor pointer; the rest
      // are the attributes
      for (size_t index = 0; index < cls->attributes.size(); index++) {
        const auto& attr = cls->attributes[index];
        size_t offset = cls->offset_for_attribute(index);

        if (type_has_refcount(attr.value.type)) {
          // write a destructor call
          dtor_as.write_label(string_printf("%s_delete_reference_%s", base_label.c_str(),
              attr.name.c_str()));

          // if inline refcounting is disabled, call delete_reference manually
          if (debug_flags & DebugFlag::NoInlineRefcounting) {
            dtor_as.write_mov(rdi, MemoryReference(rbx, offset));
            dtor_as.write_mov(rsi, r14);
            dtor_as.write_call(common_object_reference(void_fn_ptr(&delete_reference)));

          } else {
            string skip_label = string_printf(
                "__destructor_delete_reference_skip_%" PRIu64, offset);

            // get the object pointer
            dtor_as.write_mov(rdi, MemoryReference(rbx, offset));

            // if the pointer is NULL, do nothing
            dtor_as.write_test(rdi, rdi);
            dtor_as.write_je(skip_label);

            // decrement the refcount; if it's not zero, skip the destructor call
            dtor_as.write_lock();
            dtor_as.write_dec(MemoryReference(rdi, 0));
            dtor_as.write_jnz(skip_label);

            // call the destructor
            dtor_as.write_mov(rax, MemoryReference(rdi, 8));
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
      dtor_as.write_dec(MemoryReference(rbx, 0));

      // cheating time: "return" by jumping directly to free() so it will return
      // to the caller
      dtor_as.write_mov(rdi, rbx);
      dtor_as.write_add(rsp, 8);
      dtor_as.write_pop(rbx);
      dtor_as.write_pop(rbp);
      dtor_as.write_jmp(common_object_reference(void_fn_ptr(&free)));

      // assemble it
      multimap<size_t, string> compiled_labels;
      unordered_set<size_t> patch_offsets;
      string compiled = dtor_as.assemble(&patch_offsets, &compiled_labels);
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

void CompilationVisitor::write_code_for_value(const Value& value) {
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
    const std::vector<MemoryReference>& int_args,
    const std::vector<MemoryReference>& float_args,
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
      dests.emplace_back(rsp, (x - int_argument_register_order.size()) * 8);
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
    if ((ref.base_register == rsp) && ref.field_size) {
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
    } else if (ref.base_register == rsp) {
      MemoryReference new_ref(ref.base_register, ref.offset + rsp_adjustment,
          ref.index_register, ref.field_size);
      this->as.write_movsd(dest, new_ref);
    } else {
      this->as.write_movsd(dest, ref);
    }
  }

  // finally, call the function. the stack must be 16-byte aligned at this point
  if (this->stack_bytes_used & 0x0F) {
    throw compile_error("stack not aligned at function call", this->file_offset);
  }
  this->as.write_call(function_loc);

  // put the return value into the target register
  if (return_float) {
    if ((return_register != Register::None) && (return_register != xmm0)) {
      this->as.write_movsd(MemoryReference(return_register), xmm0);
    }
  } else {
    if ((return_register != Register::None) && (return_register != rax)) {
      this->as.write_mov(MemoryReference(return_register), rax);
    }
  }

  // reclaim any reserved stack space
  if (arg_stack_bytes) {
    this->adjust_stack(arg_stack_bytes);
  }

  this->write_pop_reserved_registers(previously_reserved_registers);
}

void CompilationVisitor::write_function_setup(const string& base_label,
    bool setup_special_regs) {
  // get ready to rumble
  this->as.write_label("__" + base_label);
  this->stack_bytes_used = 8;

  // lead-in (stack frame setup)
  this->write_push(rbp);
  this->as.write_mov(rbp, rsp);

  // figure out how much stack space is needed
  unordered_map<string, Register> int_arg_to_register;
  unordered_map<string, int64_t> int_arg_to_stack_offset;
  unordered_map<string, Register> float_arg_to_register;
  size_t arg_stack_offset = sizeof(int64_t) * (setup_special_regs ? 6 : 2); // account for ret addr, rbp, and maybe special regs
  for (size_t arg_index = 0; arg_index < this->fragment->function->args.size(); arg_index++) {
    const auto& arg = this->fragment->function->args[arg_index];

    bool is_float;
    try {
      is_float = this->local_variable_types.at(arg.name).type == ValueType::Float;
    } catch (const out_of_range&) {
      throw compile_error(string_printf("argument %s not present in local_variable_types",
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
      int_arg_to_stack_offset.emplace(arg.name, arg_stack_offset);
      arg_stack_offset += sizeof(int64_t);
    }
  }

  // reserve space for locals and special regs
  size_t num_stack_slots = this->fragment->function->locals.size() + (setup_special_regs ? 4 : 0);
  this->adjust_stack(num_stack_slots * -sizeof(int64_t));

  // save special regs if needed
  if (setup_special_regs) {
    this->as.write_mov(MemoryReference(rsp, 0), r12);
    this->as.write_mov(MemoryReference(rsp, 8), r13);
    this->as.write_mov(MemoryReference(rsp, 16), r14);
    this->as.write_mov(MemoryReference(rsp, 24), r15);
    this->as.write_mov(r12, reinterpret_cast<int64_t>(common_object_base()));
    this->as.write_mov(r13, reinterpret_cast<int64_t>(this->module->global_space));
    this->as.write_xor(r14, r14);
    this->as.write_xor(r15, r15);
  }

  // set up the local space. note that local_index starts at 0 (hence 1 during
  // the first loop) on purpose - this is the negative offset from rbp for the
  // current local
  ssize_t local_index = 0;
  for (const auto& local : this->fragment->function->locals) {
    local_index++;
    MemoryReference dest(rbp, local_index * -8);

    // if it's a float arg, write it from the xmm reg
    try {
      Register xmm_reg = float_arg_to_register.at(local.first);
      MemoryReference xmm_mem(xmm_reg);
      this->as.write_movsd(dest, xmm_mem);
      continue;
    } catch (const out_of_range&) { }

    // if it's an int arg, write it from the reg
    try {
      this->as.write_mov(dest, MemoryReference(int_arg_to_register.at(local.first)));
      continue;
    } catch (const out_of_range&) { }

    // if it's a stack arg, load it into a temp register, then save it again
    try {
      this->as.write_mov(rax, MemoryReference(rbp,
          int_arg_to_stack_offset.at(local.first)));
      this->as.write_mov(dest, rax);
      continue;
    } catch (const out_of_range&) { }

    // else, initialize it to zero
    this->as.write_mov(dest, 0);
  }

  // set up the exception block
  this->return_label = string_printf("__%s_return", base_label.c_str());
  this->exception_return_label = string_printf(
      "__%s_exception_return", base_label.c_str());
  this->as.write_label(string_printf(
      "__%s_create_except_block", base_label.c_str()));
  this->write_create_exception_block({}, this->exception_return_label);
}

void CompilationVisitor::write_function_cleanup(const string& base_label,
    bool setup_special_regs) {
  this->as.write_label(this->return_label);

  // clean up the exception block. note that this is after the return label but
  // before the destroy locals label - the latter is used when an exception
  // occurs, since _unwind_exception_internal already removes the exc block from
  // the stack
  this->write_pop(r14);
  this->adjust_stack(return_exception_block_size - sizeof(int64_t));

  // restore special regs if needed. note that it's ok to do this before
  // destroying locals because destruction cannot depend on the global space. if
  // a local has a __del__ function, it will set up its own global space pointer
  if (setup_special_regs) {
    this->write_pop(r12);
    this->write_pop(r13);
    this->write_pop(r14);
    this->write_pop(r15);
  }

  // call destructors for all the local variables that have refcounts
  this->as.write_label(this->exception_return_label);
  this->return_label.clear();
  this->exception_return_label.clear();
  for (auto it = this->fragment->function->locals.crbegin();
       it != this->fragment->function->locals.crend(); it++) {
    if (type_has_refcount(it->second.type)) {
      // we have to preserve the value in rax since it's the function's return
      // value, so store it on the stack (in the location we're destroying)
      // while we destroy the object
      this->as.write_xchg(rax, MemoryReference(rsp, 0));
      this->write_delete_reference(rax, it->second.type);
      this->write_pop(rax);
    } else {
      // no destructor; just skip it
      // TODO: we can coalesce these adds for great justice
      this->adjust_stack(8);
    }
  }

  // hooray we're done
  this->as.write_label(string_printf("__%s_leave_frame", base_label.c_str()));
  this->write_pop(rbp);

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

  if (!type_has_refcount(type)) {
    return;
  }

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
  this->write_add_reference(rax);
  this->as.write_mov(r15, rax);
  this->as.write_jmp(
      common_object_reference(void_fn_ptr(&_unwind_exception_internal)));
  this->as.write_label(skip_label);

  // fill in the refcount, destructor function and class id
  Register tmp = this->available_register_except({rax});
  MemoryReference tmp_mem(tmp);
  if (this->target_register != rax) {
    this->as.write_mov(MemoryReference(this->target_register), rax);
  }
  this->as.write_mov(MemoryReference(this->target_register, 0), 1);
  this->as.write_mov(tmp, reinterpret_cast<int64_t>(cls->destructor));
  this->as.write_mov(MemoryReference(this->target_register, 8), tmp_mem);
  this->as.write_mov(MemoryReference(this->target_register, 16), class_id);

  // zero everything else in the class, if it has any attributes
  if (initialize_attributes && (cls->instance_size() != sizeof(InstanceObject))) {
    this->as.write_xor(tmp_mem, tmp_mem);
    for (ssize_t x = sizeof(InstanceObject); x < cls->instance_size(); x += 8) {
      this->as.write_mov(MemoryReference(this->target_register, x), tmp_mem);
    }
  }
}

void CompilationVisitor::write_raise_exception(int64_t class_id,
    const wchar_t* message) {
  const auto* cls = this->global->context_for_class(class_id);

  this->write_alloc_class_instance(class_id, false);

  if (message) {
    // this form can only be used for exceptions that take exactly one argument
    if (cls->instance_size() != sizeof(InstanceObject) + 2 * sizeof(void*)) {
      throw compile_error("incorrect exception raise form generated", this->file_offset);
    }

    // set message
    size_t message_index = cls->attribute_indexes.at("message");
    size_t message_offset = cls->offset_for_attribute(message_index);
    const UnicodeObject* constant = this->global->get_or_create_constant(message);
    this->as.write_mov(r15, reinterpret_cast<int64_t>(constant));
    this->as.write_mov(MemoryReference(this->target_register, message_offset), r15);

  } else {
    // this form can only be used for exceptions that don't take an argument
    if (cls->instance_size() != sizeof(InstanceObject) + sizeof(void*)) {
      throw compile_error("incorrect exception raise form generated", this->file_offset);
    }
  }

  // set __init__
  size_t init_index = cls->attribute_indexes.at("__init__");
  size_t init_offset = cls->offset_for_attribute(init_index);
  const auto* cls_init = this->global->context_for_function(class_id);
  this->as.write_mov(r15, reinterpret_cast<int64_t>(cls_init));
  this->as.write_mov(MemoryReference(this->target_register, init_offset), r15);

  this->as.write_mov(r15, MemoryReference(this->target_register));

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

  this->write_push(r13);
  this->write_push(r12);

  this->write_push(rbp);
  this->write_push(tmp_rsp);
  this->write_push(r14);
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

void CompilationVisitor::write_read_variable(Register target_register,
    Register float_target_register, const VariableLocation& loc) {
  MemoryReference variable_mem = loc.variable_mem;

  // if variable_mem isn't valid, we're reading an attribute from a different
  // module; we need to get the module's global space pointer and then look up
  // the attribute
  if (!loc.variable_mem_valid) {
    this->as.write_mov(target_register, reinterpret_cast<int64_t>(loc.global_module->global_space));
    variable_mem = MemoryReference(target_register, loc.global_index * sizeof(int64_t));
  }

  if (loc.type.type == ValueType::Float) {
    this->as.write_movq_to_xmm(float_target_register, variable_mem);
  } else {
    this->as.write_mov(MemoryReference(target_register), variable_mem);
    if (type_has_refcount(loc.type.type)) {
      this->write_add_reference(target_register);
    }
  }
}

void CompilationVisitor::write_write_variable(Register value_register,
    Register float_value_register, const VariableLocation& loc) {
  MemoryReference variable_mem = loc.variable_mem;

  // if variable_mem isn't valid, we're writing an attribute on a different
  // module; we need to get the module's global space pointer and then look up
  // the attribute
  Register target_module_global_space_reg;
  if (!loc.variable_mem_valid) {
    target_module_global_space_reg = this->available_register_except({value_register});
    this->as.write_mov(target_module_global_space_reg, reinterpret_cast<int64_t>(loc.global_module->global_space));
    variable_mem = MemoryReference(target_module_global_space_reg, loc.global_index * sizeof(int64_t));
  }

  // if the type has a refcount, delete the old value
  if (type_has_refcount(loc.type.type)) {
    this->write_delete_reference(variable_mem, loc.type.type);
  }

  // write the value into the right attribute
  if (loc.type.type == ValueType::Float) {
    this->as.write_movsd(variable_mem, MemoryReference(float_value_register));
  } else {
    this->as.write_mov(variable_mem, MemoryReference(value_register));
  }
}

CompilationVisitor::VariableLocation CompilationVisitor::location_for_global(
    ModuleContext* module, const string& name) {
  try {
    const auto& var = module->global_variables.at(name);

    VariableLocation loc;
    loc.name = name;
    loc.type = var.value;
    loc.global_module = module;
    loc.global_index = var.index;
    if (loc.global_module == this->module) {
      loc.variable_mem = MemoryReference(r13, loc.global_index * sizeof(int64_t));
      loc.variable_mem_valid = true;
    }
    return loc;

  } catch (const out_of_range&) {
    throw compile_error("nonexistent global: " + name, this->file_offset);
  }
}

CompilationVisitor::VariableLocation CompilationVisitor::location_for_variable(
    const string& name) {

  // if we're writing a global, use its global slot offset (from R13)
  if (this->fragment->function &&
      this->fragment->function->explicit_globals.count(name) &&
      this->fragment->function->locals.count(name)) {
    throw compile_error("explicit global is also a local", this->file_offset);
  }
  if (!this->fragment->function || !this->fragment->function->locals.count(name)) {
    return this->location_for_global(this->module, name);
  }

  // if we're writing a local, use its local slot offset (from RBP)
  auto it = this->fragment->function->locals.find(name);
  if (it == this->fragment->function->locals.end()) {
    throw compile_error("nonexistent local: " + name, this->file_offset);
  }

  VariableLocation loc;
  loc.name = name;
  loc.variable_mem = MemoryReference(rbp, sizeof(int64_t) * (-static_cast<ssize_t>(1 + distance(this->fragment->function->locals.begin(), it))));
  loc.variable_mem_valid = true;

  // use the argument type if given
  try {
    loc.type = this->local_variable_types.at(name);
  } catch (const out_of_range&) {
    loc.type = it->second;
  }

  return loc;
}

CompilationVisitor::VariableLocation CompilationVisitor::location_for_attribute(
    ClassContext* cls, const string& name, Register instance_reg) {

  VariableLocation loc;
  loc.name = name;
  try {
    size_t index = cls->attribute_indexes.at(name);
    loc.variable_mem = MemoryReference(instance_reg, cls->offset_for_attribute(index));
    loc.variable_mem_valid = true;
    loc.type = cls->attributes.at(index).value;
  } catch (const out_of_range& e) {
    throw compile_error("cannot generate lookup for missing attribute " + name,
        this->file_offset);
  }
  return loc;
}
