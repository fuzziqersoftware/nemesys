#pragma once

#include <stdint.h>

#include <map>
#include <string>

#include "../Compiler/Exception.hh"
#include "Reference.hh"


struct InstanceObject {
  BasicObject basic;
  int64_t class_id;
  void* attributes[0];

  int64_t get_attribute_int(size_t index);
  double get_attribute_float(size_t index);
  void* get_attribute_object(size_t index);

  void set_attribute_int(size_t index, int64_t value);
  void set_attribute_float(size_t index, double value);
  void set_attribute_object(size_t index, void* value);
};

// shortcuts for common instance objects

// create an instance with no attributes
InstanceObject* create_instance(int64_t class_id, size_t attribute_count = 0);

// create an instance with a single trivial attribute
InstanceObject* create_single_attr_instance(int64_t class_id, int64_t attribute_value);
InstanceObject* create_single_attr_instance(int64_t class_id, double attribute_value);
InstanceObject* create_single_attr_instance(int64_t class_id, void* attribute_value);
