#pragma once

#include <stdint.h>

#include <string>
#include <vector>


class SourceFile {
public:
  explicit SourceFile(const std::string& filename);
  SourceFile(const SourceFile&) = default;
  SourceFile(SourceFile&&) = default;
  SourceFile& operator=(const SourceFile&) = default;
  SourceFile& operator=(SourceFile&&) = default;
  ~SourceFile() = default;

  const std::string& filename() const;

  size_t file_size() const;
  size_t line_count() const;

  const std::string& data() const;

  std::string line(size_t line_num) const;
  size_t line_offset(size_t line_num) const;
  size_t line_end_offset(size_t line_num) const;

  size_t line_number_of_offset(size_t offset) const;

private:
  std::string original_filename;
  std::string contents;
  std::vector<size_t> line_begin_offset;
};
