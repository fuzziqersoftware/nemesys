#include "List.hh"

#include <inttypes.h>
#include <stdlib.h>

#include <phosg/Strings.hh>

using namespace std;


static void list_delete(ListObject* l) {
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

ListObject* list_new(ListObject* l, uint64_t count, bool items_are_objects) {
  if (!l) {
    l = reinterpret_cast<ListObject*>(malloc(sizeof(ListObject)));
  }
  if (!l) {
    return NULL;
  }
  l->basic.refcount = 1;
  l->basic.destructor = reinterpret_cast<void (*)(void*)>(list_delete);
  l->count = count;
  l->items_are_objects = items_are_objects;
  if (l->count) {
    l->items = reinterpret_cast<void**>(malloc(l->count * sizeof(void*)));
  } else {
    l->items = NULL;
  }
  return l;
}


void* list_get_item(const ListObject* l, int64_t position) {
  if (position < 0) {
    position += l->count;
  }
  if ((position < 0) || (position >= l->count)) {
    // TODO: throw an exception
    return NULL;
  }
  return l->items[position];
}

void list_set_item(ListObject* l, int64_t position, void* value) {
  if (position < 0) {
    position += l->count;
  }
  if ((position < 0) || (position >= l->count)) {
    // TODO: throw an exception
    return;
  }
  if (l->items_are_objects && l->items[position]) {
    delete_reference(l->items[position]);
  }
  l->items[position] = value;
  add_reference(l->items[position]);
}

void list_insert(ListObject* l, int64_t position, void* value) {
  throw runtime_error("list_insert not yet implemented");
}

void list_append(ListObject* l, void* value) {
  list_insert(l, l->count, value);
}

void* list_pop(ListObject* l, int64_t position) {
  throw runtime_error("list_pop not yet implemented");
}

void list_resize(ListObject* l, uint64_t count) {
  throw runtime_error("list_resize not yet implemented");
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
