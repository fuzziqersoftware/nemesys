#include "Reference.hh"

#include <stdio.h>
#include <stdlib.h>

#include <atomic>

#include "../Debug.hh"

using namespace std;


BasicObject::BasicObject() : refcount(1), destructor(NULL) { }
BasicObject::BasicObject(void (*destructor)(void*)) : refcount(1),
    destructor(destructor) { }


void* add_reference(void* o) {
  BasicObject* obj = reinterpret_cast<BasicObject*>(o);
  if (debug_flags & DebugFlag::ShowRefcountChanges) {
    fprintf(stderr, "[refcount] %p++ == %" PRId64 "\n", o, obj->refcount++);
  } else {
    obj->refcount++;
  }
  return o;
}

void delete_reference(void* o, ExceptionBlock* exc_block) {
  BasicObject* obj = reinterpret_cast<BasicObject*>(o);
  if (!obj) {
    return;
  }

  int64_t count = --obj->refcount;
  if (debug_flags & DebugFlag::ShowRefcountChanges) {
    fprintf(stderr, "[refcount] %p-- == %" PRId64 "%s\n", o, count,
        (count == 0) ? " (destroying)" : "");
  }

  if (count == 0) {
    // TODO: pass the exception block into the destructor somehow
    obj->destructor(o);
  }
}
