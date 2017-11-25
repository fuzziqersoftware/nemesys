#include "CodeBuffer.hh"

#include <errno.h>
#include <string.h>
#include <sys/mman.h>

#include <phosg/Strings.hh>
#include <string>

using namespace std;


CodeBuffer::CodeBuffer(size_t block_size) : size(0), used_bytes(0),
    block_size(block_size) { }

void* CodeBuffer::append(const string& data,
    const unordered_set<size_t>* patch_offsets) {
  return this->append(data.data(), data.size(), patch_offsets);
}

void* CodeBuffer::append(const void* data, size_t size,
    const unordered_set<size_t>* patch_offsets) {
  // find the block with the least free space that this function can fit in
  auto block_it = this->free_bytes_to_block.lower_bound(size);
  if (block_it != this->free_bytes_to_block.end()) {
    shared_ptr<Block> block = block_it->second;
    this->free_bytes_to_block.erase(block_it);
    this->free_bytes_to_block.emplace(block->size - block->used_bytes, block);
    this->used_bytes += size;
    return block->append(data, size, patch_offsets);
  }

  // the function doesn't fit in any existing block, so make a new one
  size_t new_block_size = (size > this->block_size) ?
      (size + 0x0FFF) & 0xFFFFFFFFFFFFF000 : this->block_size;
  shared_ptr<Block> block(new Block(new_block_size));
  this->free_bytes_to_block.emplace(new_block_size - size, block);
  this->size += new_block_size;
  this->used_bytes += size;
  return block->append(data, size, patch_offsets);
}

size_t CodeBuffer::total_size() const {
  return this->size;
}

size_t CodeBuffer::total_used_bytes() const {
  return this->used_bytes;
}

CodeBuffer::Block::Block(size_t size) : size(size), used_bytes(0) {
  this->data = mmap(NULL, this->size, PROT_READ | PROT_EXEC,
      MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (this->data == MAP_FAILED) {
    string error_str = string_for_error(errno);
    throw runtime_error(string_printf("mmap failed: %s", error_str.c_str()));
  }
}

CodeBuffer::Block::~Block() {
  if (this->data != MAP_FAILED) {
    munmap(this->data, this->size);
  }
}

void* CodeBuffer::Block::append(const void* data, size_t size,
    const unordered_set<size_t>* patch_offsets) {
  if (this->size - this->used_bytes < size) {
    throw logic_error(string_printf("block cannot accept more data (%zu bytes, %zu used, %zu requested)",
        this->size, this->used_bytes, size));
  }

  void* dest = reinterpret_cast<uint8_t*>(this->data) + this->used_bytes;
  this->used_bytes += size;

  mprotect(this->data, this->size, PROT_READ | PROT_WRITE | PROT_EXEC);
  memcpy(dest, data, size);
  if (patch_offsets) {
    size_t delta = reinterpret_cast<ssize_t>(dest);
    for (size_t offset : *patch_offsets) {
      *reinterpret_cast<size_t*>(reinterpret_cast<uint8_t*>(dest) + offset) +=
          delta;
    }
  }
  mprotect(this->data, this->size, PROT_READ | PROT_EXEC);
  return dest;
}
