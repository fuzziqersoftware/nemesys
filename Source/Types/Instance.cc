#include "Instance.hh"

#include <stdint.h>

#include <map>
#include <string>
#include <stdexcept>

using namespace std;



int64_t InstanceObject::get_attribute_int(size_t index) {
  return *reinterpret_cast<int64_t*>(&this->attributes[index]);
}

double InstanceObject::get_attribute_float(size_t index) {
  return *reinterpret_cast<double*>(&this->attributes[index]);
}

void* InstanceObject::get_attribute_object(size_t index) {
  return *reinterpret_cast<void**>(&this->attributes[index]);
}

void InstanceObject::set_attribute_int(size_t index, int64_t value) {
  *reinterpret_cast<int64_t*>(&this->attributes[index]) = value;
}

void InstanceObject::set_attribute_float(size_t index, double value) {
  *reinterpret_cast<double*>(&this->attributes[index]) = value;
}

void InstanceObject::set_attribute_object(size_t index, void* value) {
  *reinterpret_cast<void**>(&this->attributes[index]) = value;
}



// create an instance with no attributes

InstanceObject* create_instance(int64_t class_id, size_t attribute_count) {
  InstanceObject* i = reinterpret_cast<InstanceObject*>(malloc(
      sizeof(InstanceObject) + attribute_count * sizeof(int64_t)));
  if (!i) {
    throw bad_alloc();
  }

  i->basic.refcount = 1;
  i->basic.destructor = &free;
  i->class_id = class_id;
  return i;
}



// create an instance with a single attribute

InstanceObject* create_single_attr_instance(int64_t class_id, int64_t attribute_value) {
  InstanceObject* i = create_instance(class_id, 1);
  if (!i) {
    throw bad_alloc();
  }
  *reinterpret_cast<int64_t*>(i + 1) = attribute_value;
  return i;
}

InstanceObject* create_single_attr_instance(int64_t class_id, double attribute_value) {
  InstanceObject* i = create_instance(class_id, 1);
  if (!i) {
    throw bad_alloc();
  }
  *reinterpret_cast<double*>(i + 1) = attribute_value;
  return i;
}

InstanceObject* create_single_attr_instance(int64_t class_id, void* attribute_value) {
  InstanceObject* i = create_instance(class_id, 1);
  if (!i) {
    throw bad_alloc();
  }

  i->basic.destructor = +[](void* o) {
    InstanceObject* i = reinterpret_cast<InstanceObject*>(o);
    BasicObject* b = *reinterpret_cast<BasicObject**>(i + 1);
    if (b) {
      b->destructor(b);
    }
    free(i);
  };

  *reinterpret_cast<void**>(i + 1) = attribute_value;
  return i;
}
