#pragma once

#include <map>
#include <memory>
#include <string>
#include <unordered_set>


class CodeBuffer {
public:
  CodeBuffer(size_t block_size = 64 * 1024);
  ~CodeBuffer() = default;

  // note: these don't return const void* because the c++ compiler complains
  // about casting away constness when you reinterpret_cast them. if you're dumb
  // enough to try to write to this pointer then you deserve your segfault
  void* append(const std::string& data,
      const std::unordered_set<size_t>* patch_offsets = NULL);
  void* append(const void* data, size_t size,
      const std::unordered_set<size_t>* patch_offsets = NULL);

  void* overwrite(void* where, const std::string& data,
      const std::unordered_set<size_t>* patch_offsets = NULL);
  void* overwrite(void* where, const void* data, size_t size,
      const std::unordered_set<size_t>* patch_offsets = NULL);

  size_t total_size() const;
  size_t total_used_bytes() const;

private:
  struct Block {
    void* data;
    size_t size;
    size_t used_bytes;

    Block(size_t size);
    Block(const Block&) = delete;
    Block(Block&&) = delete; // this could be implemented but I'm lazy
    Block& operator=(const Block&) = delete;
    Block& operator=(Block&&) = delete; // this could be implemented but I'm lazy
    ~Block();

    void* append(const void* data, size_t size,
        const std::unordered_set<size_t>* patch_offsets = NULL);
    void* overwrite(size_t offset, const void* data, size_t size,
        const std::unordered_set<size_t>* patch_offsets = NULL);
  };

  size_t size;
  size_t used_bytes;
  size_t block_size;
  std::multimap<size_t, std::shared_ptr<Block>> free_bytes_to_block;
  std::map<void*, std::shared_ptr<Block>> addr_to_block;
};
