#include "Reference.hh"

#include <stdlib.h>

#include <atomic>

using namespace std;


BasicObject::BasicObject(void (*destructor)(void*)) : refcount(1),
    destructor(destructor) { }


void* add_reference(void* o) {
  BasicObject* obj = reinterpret_cast<BasicObject*>(o);
  obj->refcount++;
  return o;
}

void delete_reference(void* o) {
  BasicObject* obj = reinterpret_cast<BasicObject*>(o);
  if (--obj->refcount == 0) {
    obj->destructor(o);
  }
}
