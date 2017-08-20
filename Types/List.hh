#pragma once

#include <stdint.h>

#include <atomic>
#include <string>
#include <vector>

#include "Reference.hh"


struct ListObject {
  BasicObject basic;
  uint64_t count;
  bool items_are_objects;
  void** items;
};

ListObject* list_new(ListObject* l, uint64_t count, bool items_are_objects);

void* list_get_item(const ListObject* l, int64_t position);
void list_set_item(ListObject* l, int64_t position, void* value);
void list_insert(ListObject* l, int64_t position, void* value);
void* list_pop(ListObject* l, int64_t position);
void list_resize(ListObject* l, uint64_t count);
void list_clear(ListObject* l);

size_t list_size(const ListObject* d);
