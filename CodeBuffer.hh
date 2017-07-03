#pragma once

#include <map>
#include <string>


class CodeBuffer {
public:
  CodeBuffer(size_t block_size = 64 * 1024);
  ~CodeBuffer() = default;

  void* append(const std::string& data);
  void* append(const void* data, size_t size);

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

    void* append(const void* data, size_t size);
  };

  size_t size;
  size_t used_bytes;
  size_t block_size;
  std::multimap<size_t, std::shared_ptr<Block>> free_bytes_to_block;
};
