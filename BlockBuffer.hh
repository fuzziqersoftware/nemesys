#pragma once

#include <string>


class BlockBuffer {
public:
  BlockBuffer(bool writable, bool executable,
      size_t block_size = 64 * 1024);
  ~BlockBuffer() = default;

  void* append(const std::string& data);
  void* append(const void* data, size_t size);

  void set_protection(bool writable, bool readable);

  size_t total_size() const;
  size_t total_free_bytes() const;

private:
  struct Block {
    void* data;
    size_t size;
    size_t free_bytes;
    int protection;

    Block(size_t size, int protection);
    Block(const Block&) = delete;
    Block(Block&&) = delete; // this could be implemented but I'm lazy
    Block& operator=(const Block&) = delete;
    Block& operator=(Block&&) = delete; // this could be implemented but I'm lazy
    ~Block();

    void* append(const void* data, size_t size);

    void set_protection(int protection);
  };

  int protection;
  size_t block_size;
  std::multimap<size_t, shared_ptr<Block>> free_bytes_to_block;
};
