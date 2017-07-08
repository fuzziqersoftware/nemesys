#pragma once

#include <stdint.h>

#include <atomic>

struct BasicObject {
  std::atomic<uint64_t> refcount;
  void (*destructor)(void*);

  BasicObject(void (*destructor)(void*));
};

// for convenience, add_reference returns o (so callers don't have to push it
// onto the stack to keep it)
void* add_reference(void* o);
void delete_reference(void* o);
