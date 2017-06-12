#include "ExecutableBuffer.hh"

#include <string>

using namespace std;


BlockBuffer::BlockBuffer(bool writable, bool executable, size_t block_size) :
    writable(writable), executable(executable), block_size(block_size) { }

void* BlockBuffer::append(const std::string& data) {
  return this->append(data.data(), data.size());
}

void* BlockBuffer::append(const void* data, size_t size) {
  // find the block with the least free space that this function can fit in
  auto block_it = this->free_bytes_to_block.lower_bound(data.size());
  if (block_it != this->free_bytes_to_block.end()) {
    shared_ptr<Block> block = block_it->second;
    void* ret = block->append(data.data(), data.size());
    this->free_bytes_to_block.erase(block_it);
    this->free_bytes_to_block.emplace(block->free_bytes, block);
    return ret;
  }

  // the function doesn't fit in any block; make a new one
  if (function_code.size() < this->default_block_size) {
    size_t block_size;
    if (function_code.size() > this->block_size) {
      // round up to the next page boundary
      block_size = (function_code.size() + 0x0FFF) & 0xFFFFFFFFFFFFF000;
    } else {
      // use the default block size
      block_size = this->block_size;
    }
    auto block = this->free_bytes_to_block.emplace(
        block_size - function_code.size(),
        new Block(block_size, this->protection)).first->second;
    return block->append(function_code);
  }
}

void BlockBuffer::set_protection(bool writable, bool executable) {
  int new_protection = PROT_READ | (writable ? PROT_WRITE : 0) |
      (executable ? PROT_EXEC : 0);
  for (auto& block_it : this->free_bytes_to_block) {
    block_it->second->set_protection(new_protection);
  }
}

size_t total_size() const {
  size_t ret;
  for (const auto& block_it : this->free_bytes_to_block) {
    ret += block_it->second->size;
  }
  return ret;
}

size_t total_free_bytes() const {
  size_t ret;
  for (const auto& block_it : this->free_bytes_to_block) {
    ret += block_it->second->free_bytes;
  }
  return ret;
}

BlockBuffer::Block::Block(size_t size, int protection) : size(size),
    free_bytes(size), protection(protection) {
  this->data = mmap(NULL, this->size, this->protection,
      MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (this->data == MAP_FAILED) {
    string error_str = string_for_error(errno);
    throw runtime_error("mmap failed: %s", error_str.c_str());
  }
}

BlockBuffer::Block::~Block() {
  if (this->data != MAP_FAILED) {
    munmap(this->data, this->size);
  }
}

void* BlockBuffer::Block::append(const void* data, size_t size) {
  if (this->free_bytes < size) {
    throw logic_error("block cannot accept more data");
  }

  void* ret = reinterpret_cast<uint8_t*>(this->data) + this->size -
      this->free_bytes;
  this->free_bytes -= size;
  if (!(this->protection & PROT_WRITE)) {
    int original_protection = this->protection;
    this->set_protection(this->protection | PROT_WRITE);
    memcpy(ret, data, size);
    this->set_protection(original_protection);
  } else {
    memcpy(ret, data, size);
  }
  return ret;
}

void BlockBuffer::Block::set_protection(int protection) {
  this->protection = protection;
  mprotect(this->data, this->size, this->protection);
}
