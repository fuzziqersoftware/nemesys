#include "List.hh"

#include <inttypes.h>
#include <stdlib.h>

#include <phosg/Strings.hh>

#include "../BuiltinFunctions.hh"

using namespace std;


ListObject* list_new(ListObject* l, uint64_t count, bool items_are_objects,
    ExceptionBlock* exc_block) {
  if (!l) {
    l = reinterpret_cast<ListObject*>(malloc(sizeof(ListObject)));
  }
  if (!l) {
    raise_python_exception(exc_block, &MemoryError_instance);
    throw bad_alloc();
  }
  l->basic.refcount = 1;
  l->basic.destructor = reinterpret_cast<void (*)(void*)>(list_delete);
  l->count = count;
  l->capacity = count;
  l->items_are_objects = items_are_objects;
  if (l->count) {
    l->items = reinterpret_cast<void**>(malloc(l->count * sizeof(void*)));
  } else {
    l->items = NULL;
  }
  return l;
}

void list_delete(ListObject* l) {
  if (l->items) {
    if (l->items_are_objects) {
      for (uint64_t x = 0; x < l->count; x++) {
        delete_reference(l->items[x]);
      }
    }
    free(l->items);
  }
  free(l);
}


void* list_get_item(const ListObject* l, int64_t position,
    ExceptionBlock* exc_block) {
  if (position < 0) {
    position += l->count;
  }
  if ((position < 0) || (position >= l->count)) {
    raise_python_exception(exc_block, create_instance(IndexError_class_id));
    throw out_of_range("index out of range for list object");
  }
  return l->items[position];
}

void list_set_item(ListObject* l, int64_t position, void* value,
    ExceptionBlock* exc_block) {
  if (position < 0) {
    position += l->count;
  }
  if ((position < 0) || (position >= l->count)) {
    raise_python_exception(exc_block, create_instance(IndexError_class_id));
    throw out_of_range("index out of range for list object");
  }
  if (l->items_are_objects && l->items[position]) {
    delete_reference(l->items[position]);
  }
  l->items[position] = value;
  add_reference(l->items[position]);
}

void list_insert(ListObject* l, int64_t position, void* value,
    ExceptionBlock* exc_block) {

  if (position < 0) {
    position += l->count;
  }
  if (position < 0 || position > l->count) {
    raise_python_exception(exc_block, create_instance(IndexError_class_id));
    throw out_of_range("index out of range for list insert");
  }

  if (l->count <= l->capacity) {
    memmove(&l->items[position + 1], &l->items[position],
        (l->count - position) * sizeof(void*));
    l->items[position] = value;
    l->count++;

  } else {
    // TODO: maybe we can do something smarter than just doubling the capacity
    size_t new_capacity = (l->capacity == 0) ? 1 : (2 * l->capacity);
    void** new_items = reinterpret_cast<void**>(malloc(new_capacity * sizeof(void*)));
    if (!new_items) {
      raise_python_exception(exc_block, &MemoryError_instance);
      throw bad_alloc();
    }

    memcpy(new_items, l->items, position * sizeof(void*));
    new_items[position] = value;
    memcpy(&new_items[position + 1], &l->items[position],
        (l->count - position) * sizeof(void*));

    free(l->items);
    l->items = new_items;
    l->capacity = new_capacity;
    l->count++;
  }
}

void list_append(ListObject* l, void* value, ExceptionBlock* exc_block) {
  list_insert(l, l->count, value, exc_block);
}

void* list_pop(ListObject* l, int64_t position, ExceptionBlock* exc_block) {
  if (position < 0) {
    position += l->count;
  }
  if (position < 0 || position >= l->count) {
    raise_python_exception(exc_block, create_instance(IndexError_class_id));
    throw out_of_range("index out of range for list pop");
  }

  void* ret = l->items[position];

  // if less than 50% of the list will be in use after popping, shrink it to fit
  if (l->count <= l->capacity / 2) {
    void** new_items = reinterpret_cast<void**>(malloc((l->count - 1) * sizeof(void*)));
    if (!new_items) {
      raise_python_exception(exc_block, &MemoryError_instance);
      throw bad_alloc();
    }
    memcpy(new_items, l->items, position * sizeof(void*));
    memcpy(&new_items[position], &l->items[position + 1],
        (l->count - position - 1) * sizeof(void*));

  } else {
    memmove(&l->items[position], &l->items[position + 1],
        (l->count - position - 1) * sizeof(void*));
    l->count--;
  }

  return ret;
}

void list_clear(ListObject* l) {
  if (l->items_are_objects) {
    for (uint64_t x = 0; x < l->count; x++) {
      delete_reference(l->items[x]);
    }
  }
  free(l->items);
  l->items = NULL;
  l->count = 0;
}

size_t list_size(const ListObject* l) {
  return l->count;
}
