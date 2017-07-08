#include "SourceFile.hh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <phosg/Filesystem.hh>

using namespace std;


SourceFile::SourceFile(const string& filename, bool is_data) :
    original_filename(is_data ? "__imm__" : filename),
    contents(is_data ? filename : load_file(this->original_filename)) {

  // find the start offsets of all the lines
  size_t last_line_start = 0;
  for (size_t x = 0; x < this->contents.size(); x++) {
    if (this->contents[x] == '\n') {
      this->line_begin_offset.push_back(last_line_start);
      last_line_start = x + 1;
    }
  }
  this->line_begin_offset.push_back(last_line_start);
}

const string& SourceFile::data() const {
  return this->contents;
}

string SourceFile::line(size_t line_num) const {
  if (line_num == 0) {
    throw out_of_range("line numbers are 1-based, not 0-based");
  }
  if (line_num > this->line_begin_offset.size()) {
    throw out_of_range("line is beyond end of file");
  }

  size_t line_start = this->line_begin_offset[line_num - 1];
  size_t line_end;
  if (line_num == this->line_begin_offset.size()) {
    line_end = this->contents.size();
  } else {
    line_end = this->line_begin_offset[line_num];
  }

  // trim off \n characters
  while ((line_end > line_start) && (line_end > 0) &&
         (this->contents[line_end - 1] == '\n')) {
    line_end--;
  }

  return this->contents.substr(line_start, line_end - line_start);
}

size_t SourceFile::line_offset(size_t line_num) const {
  if (line_num == 0) {
    throw out_of_range("line numbers are 1-based, not 0-based");
  }
  if (line_num > this->line_begin_offset.size()) {
    throw out_of_range("line is beyond end of file");
  }
  return this->line_begin_offset[line_num - 1];
}

size_t SourceFile::line_end_offset(size_t line_num) const {
  if (line_num == 0) {
    throw out_of_range("line numbers are 1-based, not 0-based");
  }
  if (line_num > this->line_begin_offset.size()) {
    throw out_of_range("line is beyond end of file");
  }
  if (line_num == this->line_begin_offset.size()) {
    return this->contents.size();
  }
  // the -1 trims off the \n
  return this->line_begin_offset[line_num - 1] - 1;
}

const string& SourceFile::filename() const {
  return this->original_filename;
}

size_t SourceFile::file_size() const {
  return this->contents.size();
}

size_t SourceFile::line_count() const {
  return this->line_begin_offset.size();
}

size_t SourceFile::line_number_of_offset(size_t offset) const {
  if (offset >= this->contents.size()) {
    return -1;
  }

  // TODO: this should be binary search but I'm too lazy to figure out
  // std::lower_bound right now
  size_t line_num = 0;
  for (size_t x = 0; x < this->line_begin_offset.size() - 1; x++) {
    if (this->line_begin_offset[line_num + 1] <= offset) {
      line_num++;
    }
  }
  return line_num + 1;
}
