#include "Dictionary.hh"

#include <stdlib.h>

#include <phosg/Strings.hh>

#include "Instance.hh"

using namespace std;

extern InstanceObject MemoryError_instance;
extern int64_t KeyError_class_id;


static size_t dictionary_default_key_length(const void* k) {
  // trim zero bytes off the end
  const uint8_t* b = reinterpret_cast<const uint8_t*>(&k);
  size_t x = 8;
  for (; x > 0; x--) {
    if (b[x - 1]) {
      break;
    }
  }
  return x;
}

static uint8_t dictionary_default_key_char(const void* k, size_t which) {
  return reinterpret_cast<const uint8_t*>(&k)[which];
}

static void dictionary_delete(void* d) {
  dictionary_clear(reinterpret_cast<DictionaryObject*>(d));
  free(d);
}

DictionaryObject* dictionary_new(DictionaryObject* d,
    size_t (*key_length)(const void* k),
    uint8_t (*key_char)(const void* k, size_t offset), uint64_t flags,
    ExceptionBlock* exc_block) {
  if (!d) {
    d = reinterpret_cast<DictionaryObject*>(malloc(sizeof(DictionaryObject)));
  }
  if (!d) {
    raise_python_exception(exc_block, &MemoryError_instance);
    throw bad_alloc();
  }
  d->basic.refcount = 1;
  d->basic.destructor = dictionary_delete;
  d->key_length = key_length ? key_length : dictionary_default_key_length;
  d->key_char = key_char ? key_char : dictionary_default_key_char;
  d->count = 0;
  d->node_count = 0;
  d->flags = flags;
  d->root = NULL;
  return d;
}



void dictionary_insert(DictionaryObject* d, void* k, void* v,
    ExceptionBlock* exc_block) {
  // find and clear the slot offset for the key, creating it if necessary
  auto t = d->traverse(k, false, true, exc_block);
  if (!t.node) {
    // TODO: raise a python exception
    throw logic_error("creation traversal did not yield a node");
  }

  // replace the value in the value slot
  auto slot_contents = t.node->get_slot(t.ch);
  if (slot_contents.is_subnode) {
    // TODO: raise a python exception
    throw logic_error("creation traversal yielded a slot containing a node");
  }
  if (slot_contents.occupied) {
    if (d->flags & DictionaryFlag::KeysAreObjects) {
      delete_reference(slot_contents.key);
    }
    if (d->flags & DictionaryFlag::ValuesAreObjects) {
      delete_reference(slot_contents.value);
    }
  } else {
    d->count++;
  }
  t.node->set_slot(t.ch, k, v, true, false);

  // track the new reference
  if (d->flags & DictionaryFlag::KeysAreObjects) {
    add_reference(k);
  }
  if (d->flags & DictionaryFlag::ValuesAreObjects) {
    add_reference(v);
  }
}

bool dictionary_erase(DictionaryObject* d, void* k) {
  // find the value slot for this key, tracking the node path as we go
  auto t = d->traverse(k, true);
  if (!t.node) {
    return false; // slot doesn't exist, so the key doesn't exist
  }

  // check if the value exists
  auto slot_contents = t.node->get_slot(t.ch);
  if (!slot_contents.occupied) {
    return false; // the value already doesn't exist
  }

  // delete the value
  if (d->flags & DictionaryFlag::KeysAreObjects) {
    delete_reference(slot_contents.key);
  }
  if (d->flags & DictionaryFlag::ValuesAreObjects) {
    delete_reference(slot_contents.value);
  }
  d->count--;
  t.node->set_slot(t.ch, NULL, NULL, false, false);

  // delete all empty nodes on the path, except the root, starting from the leaf
  while (t.nodes.size() > 1) {
    DictionaryObject::Node* parent_node = t.nodes[t.nodes.size() - 2];
    DictionaryObject::Node* node = t.nodes.back();
    if (node->has_children()) {
      break;
    }

    // the node has no children, but may have a value. unlink this node from the
    // parent and move its value to its slot in the parent. no need to mess with
    // refcounts since we're just moving an existing reference.
    bool node_had_value = node->has_value;
    if (node_had_value) {
      parent_node->set_slot(node->parent_slot, node->key, node->value, true, false);
    } else {
      parent_node->set_slot(node->parent_slot, NULL, NULL, false, false);
    }

    // delete the child node
    free(node);
    d->node_count--;

    // if the node had a value, we're done - the parent node is not empty since
    // we just put the value in a child slot there
    if (node_had_value) {
      break;
    }

    t.nodes.pop_back();
  }

  // if we made it to the root and the root is empty, delete it
  if ((t.nodes.size() == 1) && !d->root->has_children() && !d->root->has_value) {
    free(d->root);
    d->root = NULL;
    d->node_count--;
  }

  return true;
}


void dictionary_clear(DictionaryObject* d) {
  if (!d->root) {
    return;
  }

  bool keys_are_objects = d->flags & DictionaryFlag::KeysAreObjects;
  bool values_are_objects = d->flags & DictionaryFlag::ValuesAreObjects;

  vector<DictionaryObject::Node*> node_stack;
  node_stack.emplace_back(d->root);
  while (!node_stack.empty()) {
    DictionaryObject::Node* node = node_stack.back();
    node_stack.pop_back();

    if (node->has_value) {
      if (keys_are_objects) {
        delete_reference(node->key);
      }
      if (values_are_objects) {
        delete_reference(node->value);
      }
    }

    for (uint16_t x = node->start; x <= node->end; x++) {
      auto slot_contents = node->get_slot(x);
      if (!slot_contents.occupied) {
        continue;
      }

      if (slot_contents.is_subnode) {
        node_stack.emplace_back(
            reinterpret_cast<DictionaryObject::Node*>(slot_contents.value));
      } else {
        if (keys_are_objects) {
          delete_reference(slot_contents.key);
        }
        if (values_are_objects) {
          delete_reference(slot_contents.value);
        }
      }
    }

    free(node);
  }

  d->root = NULL;
  d->count = 0;
  d->node_count = 0;
}


bool dictionary_exists(const DictionaryObject* d, void* k) {
  auto t = d->traverse(k, false);
  if (!t.node) {
    return false;
  }
  return t.node->get_slot(t.ch).occupied;
}


void* dictionary_at(const DictionaryObject* d, void* k,
    ExceptionBlock* exc_block) {
  // find the value slot for this key
  auto t = d->traverse(k, false);
  if (!t.node) {
    raise_python_exception(exc_block, create_instance(KeyError_class_id));
    throw out_of_range("key does not exist in dictionary");
  }

  // get the contents and convert them into something we can return
  auto slot_contents = t.node->get_slot(t.ch);
  if (!slot_contents.occupied || slot_contents.is_subnode) {
    raise_python_exception(exc_block, create_instance(KeyError_class_id));
    throw out_of_range("key does not exist in dictionary");
  }
  return slot_contents.value;
}


size_t dictionary_size(const DictionaryObject* d) {
  return d->count;
}

size_t dictionary_node_size(const DictionaryObject* d) {
  return d->node_count;
}


DictionaryObject::SlotContents::SlotContents() : key(NULL), value(NULL),
    occupied(false), is_subnode(false) { }

DictionaryObject::Node::Node(uint8_t start, uint8_t end, uint8_t parent_slot,
    void* key, void* value, bool has_value) : start(start), end(end),
    parent_slot(parent_slot), has_value(has_value), key(key), value(value) {
  // warning: this constructor does not clear the value slots! the caller has to
  // do this itself if it uses this constructor

  // if the number of slots isn't a multiple of 4, clear the last byte in the
  // flags array in case there are some nonzero bits
  if (((end - start) % 4) != 3) {
    *(this->flags_array() + ((end - start) / 4)) = 0;
  }
}

DictionaryObject::Node::Node(uint8_t slot, uint8_t parent_slot, void* key,
    void* value, bool has_value) : start(slot), end(slot),
    parent_slot(parent_slot), has_value(has_value), key(key), value(value) {
  this->set_slot(slot, NULL, NULL, false, false);

  // clear the last byte in the flags array in case there are some nonzero bits
  *this->flags_array() = 0;
}

size_t DictionaryObject::Node::size_for_range(uint8_t start, uint8_t end) {
  if (start > end) {
    return sizeof(Node);
  }
  size_t field_count = static_cast<uint16_t>(end) - static_cast<uint16_t>(start) + 1;
  return sizeof(Node) + field_count * 2 * sizeof(void*) +
      ((field_count + 3) / 4) * sizeof(uint8_t);
}

void** DictionaryObject::Node::fields_array() {
  return reinterpret_cast<void**>(&this->data[0]);
}

uint8_t* DictionaryObject::Node::flags_array() {
  size_t field_count = static_cast<uint16_t>(this->end) -
      static_cast<uint16_t>(this->start) + 1;
  return reinterpret_cast<uint8_t*>(&this->data[0])
      + field_count * 2 * sizeof(void*);
}

void* const * DictionaryObject::Node::fields_array() const {
  return reinterpret_cast<void* const *>(&this->data[0]);
}

const uint8_t* DictionaryObject::Node::flags_array() const {
  size_t field_count = static_cast<uint16_t>(this->end) -
      static_cast<uint16_t>(this->start) + 1;
  return reinterpret_cast<const uint8_t*>(&this->data[0])
      + field_count * 2 * sizeof(void*);
}

DictionaryObject::SlotContents DictionaryObject::Node::get_slot(
    uint16_t ch) const {
  SlotContents ret;

  if (ch > 0xFF) {
    ret.is_subnode = false;
    ret.occupied = this->has_value;
    if (this->has_value) {
      ret.key = this->key;
      ret.value = this->value;
    }
    return ret;
  }

  if ((ch < this->start) || (ch > this->end)) {
    ret.occupied = false;
    ret.is_subnode = false;
    return ret;
  }

  void* const * fields_array = this->fields_array();
  const uint8_t* flags_array = this->flags_array();

  // check the flags first
  uint8_t slot_offset = ch - this->start;
  uint8_t flags = (flags_array[slot_offset / 4] >> ((slot_offset % 4) * 2)) & 3;
  ret.occupied = flags & 2;
  ret.is_subnode = flags & 1;
  if (ret.occupied) {
    ret.key = fields_array[2 * slot_offset];
    ret.value = fields_array[2 * slot_offset + 1];
  }
  return ret;
}

void DictionaryObject::Node::set_slot(uint16_t ch, void* k, void* v,
    bool occupied, bool is_subnode) {
  if (ch > 0xFF) {
    if (is_subnode) {
      throw logic_error("setting subnode as node value");
    }
    this->has_value = occupied;
    if (occupied) {
      this->key = k;
      this->value = v;
    }
    return;
  }

  if ((ch < this->start) || (ch > this->end)) {
    throw logic_error("setting nonexistent slot");
  }

  void** fields_array = this->fields_array();
  uint8_t* flags_array = this->flags_array();

  uint8_t slot_offset = ch - this->start;
  uint8_t new_flags = (occupied << 1) | is_subnode;
  flags_array[slot_offset / 4] = (flags_array[slot_offset / 4]
      & (~(3 << ((slot_offset % 4) * 2))))
      | (new_flags << ((slot_offset % 4) * 2));
  fields_array[2 * slot_offset] = k;
  fields_array[2 * slot_offset + 1] = v;
}

bool DictionaryObject::Node::has_children() const {
  size_t field_count = static_cast<uint16_t>(this->end) -
      static_cast<uint16_t>(this->start) + 1;
  const uint8_t* flags_array = reinterpret_cast<const uint8_t*>(&this->data[0])
      + field_count * 2 * sizeof(void*);
  for (size_t x = 0; x < field_count; x += 4) {
    if (flags_array[x] != 0) {
      return true;
    }
  }
  return false;
}


DictionaryObject::Traversal DictionaryObject::traverse(void* k, bool with_nodes,
    bool create, ExceptionBlock* exc_block) {
  size_t k_len = this->key_length(k);

  Traversal t;
  if (!this->root) {
    if (!create) {
      t.node = NULL;
      return t;
    }

    this->node_count++;

    if (k_len == 0) {
      this->root = reinterpret_cast<Node*>(malloc(Node::size_for_range(1, 0)));
      if (!this->root) {
        raise_python_exception(exc_block, &MemoryError_instance);
        throw bad_alloc();
      }
      new (this->root) Node(1, 0, 0, NULL, NULL, false);
      t.node = this->root;
      t.ch = 0x100;
      if (with_nodes) {
        t.nodes.emplace_back(t.node);
      }
      return t;
    }

    uint8_t ch = this->key_char(k, 0);
    this->root = reinterpret_cast<Node*>(malloc(Node::size_for_range(ch, ch)));
    if (!this->root) {
      raise_python_exception(exc_block, &MemoryError_instance);
      throw bad_alloc();
    }
    new (this->root) Node(ch, ch, 0, NULL, NULL, false);
  }

  Node* parent_node = NULL;
  t.node = this->root;
  if (with_nodes) {
    t.nodes.reserve(k_len);
    t.nodes.emplace_back(t.node);
  }

  // follow links to the leaf node
  size_t k_offset = 0;
  while (k_offset != k_len) {
    t.ch = this->key_char(k, k_offset);

    // if the current char is out of range for this node, the key doesn't exist
    if ((t.ch < t.node->start) || (t.ch > t.node->end)) {
      break;
    }

    // if the next node is missing, the key doesn't exist
    auto slot_contents = t.node->get_slot(t.ch);
    if (!slot_contents.occupied) {
      break;
    }

    // if the next node is a value, return it only if we're at the end
    // of the key. if it's not the end, we may have to make some changes
    if (!slot_contents.is_subnode) {
      if (k_offset == k_len - 1) {
        return t;
      } else {
        break;
      }
    }

    // the next node is a subnode, not a value - move down to it
    if (with_nodes) {
      t.nodes.emplace_back(reinterpret_cast<Node*>(slot_contents.value));
    }
    parent_node = t.node;
    t.node = reinterpret_cast<Node*>(slot_contents.value);
    k_offset++;
  }

  // if the node was found and it's not a value, return the value field
  if (k_offset == k_len) {
    t.ch = 0x100;
    return t;
  }

  // the node wasn't found; fail if we're not supposed to create it
  if (!create) {
    t.node = NULL;
    return t;
  }

  // if we get here, then the node doesn't exist and we should create it.
  // everything before here should not modify the tree at all, so the traverse()
  // const method can be implemented by calling this function.

  // first check if the current node has enough available range, and replace it
  // if not
  {
    bool extend_start = (t.ch < t.node->start);
    bool needs_extend = extend_start || (t.ch > t.node->end);
    if (needs_extend) {
      // make a new node
      uint8_t new_start = extend_start ? t.ch : t.node->start;
      uint8_t new_end = (!extend_start) ? t.ch : t.node->end;
      Node* new_node = reinterpret_cast<Node*>(malloc(
          Node::size_for_range(new_start, new_end)));
      if (!new_node) {
        raise_python_exception(exc_block, &MemoryError_instance);
        throw bad_alloc();
      }
      new (new_node) Node(new_start, new_end, t.node->parent_slot, t.node->key,
          t.node->value, t.node->has_value);

      // copy the relevant data from the old node and clear the values in the
      // newly-created slots. we have to do this explicitly because the Node
      // constructor that we called doesn't clear the children slots
      uint16_t x = new_node->start;
      if (extend_start) {
        // new slots are at the low end of the range
        for (; x < t.node->start; x++) {
          new_node->set_slot(x, NULL, NULL, false, false);
        }
        for (; x <= new_node->end; x++) {
          auto c = t.node->get_slot(x);
          new_node->set_slot(x, c.key, c.value, c.occupied, c.is_subnode);
        }
      } else {
        // new slots are at the high end of the range
        for (; x <= t.node->end; x++) {
          auto c = t.node->get_slot(x);
          new_node->set_slot(x, c.key, c.value, c.occupied, c.is_subnode);
        }
        for (; x <= new_node->end; x++) {
          new_node->set_slot(x, NULL, NULL, false, false);
        }
      }

      // delete the old node. if it's the root, update this->root appropriately
      if (parent_node) {
        auto old_slot_contents = parent_node->get_slot(new_node->parent_slot);
        if (!old_slot_contents.occupied || !old_slot_contents.is_subnode) {
          throw logic_error("replaced node not found in parent");
        }
        free(old_slot_contents.value);
        parent_node->set_slot(new_node->parent_slot, NULL, new_node, true, true);
      } else { // we're replacing the root node
        free(this->root);
        this->root = new_node;
      }

      // move the new node into place and delete the old node
      t.node = new_node;

      // if we were collecting nodes, we just replaced the last one.
      // t.node_offsets is never empty here; it always contains at least the
      // root node
      if (with_nodes) {
        t.nodes.back() = t.node;
      }
    }
  }

  // now the current node contains a slot that we want to follow but it's empty,
  // so we'll create all the nodes we need. we won't create the last node
  // because we'll just stick the value in that slot.
  // note: k_len is not zero here; we would have noticed above when
  // k_offset == k_len
  while (k_offset != k_len - 1) {
    uint16_t next_ch = this->key_char(k, k_offset + 1);

    // allocate a node and make the current node point to it
    auto slot_contents = t.node->get_slot(t.ch);
    if (slot_contents.occupied && slot_contents.is_subnode) {
      throw logic_error("new leaf node replaces existing node");
    }
    Node* new_node = reinterpret_cast<Node*>(malloc(
        Node::size_for_range(next_ch, next_ch)));
    if (!new_node) {
      raise_python_exception(exc_block, &MemoryError_instance);
      throw bad_alloc();
    }
    new (new_node) Node(next_ch, t.ch, slot_contents.key, slot_contents.value,
        slot_contents.occupied);

    // link to the new node from the parent
    t.node->set_slot(t.ch, NULL, new_node, true, true);
    this->node_count++;

    // move down to that node
    if (with_nodes) {
      t.nodes.emplace_back(new_node);
    }
    t.node = new_node;
    t.ch = next_ch;
    k_offset++;
  }

  // now t refers to the node that contains the slot we want
  return t;
}

DictionaryObject::Traversal DictionaryObject::traverse(void* k,
    bool with_nodes) const {
  return const_cast<DictionaryObject*>(this)->traverse(k, with_nodes, false);
}

bool dictionary_next_item(const DictionaryObject* d,
    DictionaryObject::SlotContents* ret) {
  ret->is_subnode = false;

  DictionaryObject::Node* node = d->root;
  if (!node) {
    return false;
  }

  int16_t slot_id = 0;
  size_t k_len = ret->occupied ? d->key_length(ret->key) : 0;
  vector<DictionaryObject::Node*> nodes;
  nodes.reserve(k_len);
  nodes.emplace_back(node);

  if (!ret->occupied) {
    if (node->has_value) {
      ret->key = node->key;
      ret->value = node->value;
      ret->occupied = true;
      return true;
    }

  // current is not NULL - we're continuing iteration
  } else {
    size_t k_offset = 0;

    // follow links to the leaf node as far as possible
    while (k_offset != k_len) {
      uint8_t ch = d->key_char(ret->key, k_offset);
      // if current is before anything in this node, then we have to iterate the
      // node's children, but not the node itself (the node's value is at some
      // prefix of current, so it's not after current).
      if (ch < node->start) {
        slot_id = node->start;
        break;
      }

      // if current is after anything in this node, then we don't iterate the
      // node at all - we'll have to unwind the stack
      if (ch > node->end) {
        slot_id = 0x100;
        break;
      }

      // if the slot does not contain a subnode, we're done here; we'll start by
      // examining the following slot
      auto slot_contents = node->get_slot(ch);
      if (!slot_contents.is_subnode) {
        slot_id = ch + 1;
        break;
      }

      // slot contains a subnode, not a value - move down to it
      node = reinterpret_cast<DictionaryObject::Node*>(slot_contents.value);
      nodes.emplace_back(node);
      k_offset++;
    }
  }

  // we found the position in the tree that's immediately after the given key.
  // now find the next non-null value in the tree at or after that position.
  while (!nodes.empty()) {
    // check the node's value if we need to
    if (slot_id < 0) {
      if (node->has_value) {
        ret->occupied = true;
        ret->key = node->key;
        ret->value = node->value;
        return true;
      }
      slot_id = node->start;

    } else if (slot_id < node->start) {
      slot_id = node->start;
    }

    // if we're done with this node, go to the next slot in the parent node
    if (slot_id > node->end) {
      nodes.pop_back();
      if (nodes.empty()) {
        return false;
      }
      slot_id = node->parent_slot + 1;
      node = nodes.back();
      continue;
    }

    // if the slot is empty, keep going in this node
    auto slot_contents = node->get_slot(slot_id);
    if (!slot_contents.occupied) {
      slot_id++;
      continue;
    }

    // if the slot contains a value, we're done
    if (!slot_contents.is_subnode) {
      ret->key = slot_contents.key;
      ret->value = slot_contents.value;
      ret->occupied = true;
      return true;
    }

    // the slot contains a subnode, so move to it and check if it has a value
    node = reinterpret_cast<DictionaryObject::Node*>(slot_contents.value);
    nodes.emplace_back(node);
    slot_id = -1;
  }

  // if we didn't find a value, we're done iterating the tree
  return false;
}

string dictionary_structure(const DictionaryObject* d) {
  if (d->root == NULL) {
    return "()";
  }
  return d->structure_for_node(d->root);
}

string DictionaryObject::structure_for_node(const Node* node) const {
  string ret = string_printf("([%02hhX,%02hhX]@%02hhX+", node->start, node->end,
      node->parent_slot);
  if (node->has_value) {
    ret += 'V';
  } else {
    ret += '#';
  }

  for (uint16_t x = node->start; x <= node->end; x++) {
    auto slot_contents = node->get_slot(x);
    if (!slot_contents.occupied) {
      continue;
    }
    ret += string_printf(",%02hhX:", x);
    if (slot_contents.is_subnode) {
      ret += this->structure_for_node(reinterpret_cast<const Node*>(
          slot_contents.value));
    } else {
      ret += 'V';
    }
  }

  return ret + ')';
}
