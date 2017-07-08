#pragma once

#include <stdint.h>

#include <atomic>
#include <string>
#include <vector>

#include "Reference.hh"


enum DictionaryFlag {
  KeysAreObjects   = 0x01,
  ValuesAreObjects = 0x02,
};

struct DictionaryObject {
  BasicObject basic;

  size_t (*key_length)(const void* k);
  uint8_t (*key_char)(const void* k, size_t offset);

  uint64_t count;
  uint64_t node_count;
  uint64_t flags;

  struct SlotContents {
    void* key;
    void* value;
    bool occupied;
    bool is_subnode;
  };

  struct Node {
    uint8_t start;
    uint8_t end;
    uint8_t parent_slot;
    bool has_value;
    void* key;
    void* value;
    uint8_t data[0];

    Node(uint8_t start, uint8_t end, uint8_t parent_slot, void* key,
        void* value, bool has_value);
    Node(uint8_t slot, uint8_t parent_slot, void* key, void* value,
        bool has_value);

    static size_t size_for_range(uint8_t start, uint8_t end);

    void** fields_array();
    uint8_t* flags_array();
    void* const * fields_array() const;
    const uint8_t* flags_array() const;

    SlotContents get_slot(uint16_t ch) const;
    void set_slot(uint16_t ch, void* k, void* v, bool occupied, bool is_subnode);

    bool has_children() const;
  };
  Node* root;

  struct Traversal {
    Node* node;
    uint16_t ch;
    std::vector<Node*> nodes;
  };

  Traversal traverse(void* k, bool with_nodes, bool create);
  Traversal traverse(void* k, bool with_nodes) const;

  std::string structure_for_node(const Node* n) const;

  SlotContents next_item(void* k, bool starting = false) const;
};

DictionaryObject* dictionary_new(DictionaryObject* d,
    size_t (*key_length)(const void* k),
    uint8_t (*key_char)(const void* k, size_t which), uint64_t flags);

void dictionary_insert(DictionaryObject* d, void* k, void* v);
bool dictionary_erase(DictionaryObject* d, void* k);
void dictionary_clear(DictionaryObject* d);

bool dictionary_exists(const DictionaryObject* d, void* k);
void* dictionary_at(const DictionaryObject* d, void* k);
DictionaryObject::SlotContents dictionary_first_item(const DictionaryObject* d);
DictionaryObject::SlotContents dictionary_next_item(const DictionaryObject* d,
    void* k);
size_t dictionary_size(const DictionaryObject* d);
size_t dictionary_node_size(const DictionaryObject* d);

std::string dictionary_structure(const DictionaryObject* d);
