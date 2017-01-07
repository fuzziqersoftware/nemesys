#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "source_file.hh"

SourceFile::SourceFile(char* filename) {
  FILE* f = fopen(filename, "rt");
  if (!f) {
    this->_clear();

  } else {
    int filename_size = strlen(filename);
    fseek(f, 0, SEEK_END);
    this->_filesize = ftell(f);
    this->_filename = (char*)malloc(filename_size + 1 + this->_filesize + 1);
    if (!this->_filename) {
      this->_clear();
    } else {
      strcpy(this->_filename, filename);
      this->_contents = this->_filename + (filename_size + 1);
      fseek(f, 0, SEEK_SET);
      if (fread(this->_contents, 1, this->_filesize, f) != this->_filesize) {
        free(this->_filename);
        this->_clear();
      } else {
        this->_contents[this->_filesize] = 0;
        this->_count_lines();
      }
    }
    fclose(f);
  }
}

SourceFile::~SourceFile() {
  if (this->_filename) {
    free(this->_filename);
  }
}

const char* SourceFile::data() const {
  return this->_contents;
}

const char* SourceFile::line(size_t line_num) const {
  if (line_num >= this->_line_begin_offset.size())
    return NULL;
  return &this->_contents[this->_line_begin_offset[line_num]];
}

size_t SourceFile::line_offset(size_t line_num) const {
  if (line_num >= this->_line_begin_offset.size())
    return -1;
  return this->_line_begin_offset[line_num];
}

size_t SourceFile::line_end_offset(size_t line_num) const {
  if (line_num >= this->_line_begin_offset.size())
    return -1;
  if (line_num == this->_line_begin_offset.size() - 1)
    return this->_filesize;
  return this->_line_begin_offset[line_num + 1] - 1;
}

const char* SourceFile::filename() const {
  return this->_filename;
}

size_t SourceFile::filesize() const {
  return this->_filesize;
}

size_t SourceFile::num_lines() const {
  return this->_line_begin_offset.size();
}

size_t SourceFile::line_number_of_offset(size_t offset) const {
  if (offset >= this->_filesize)
    return -1;

  // TODO: binary search or something better than what's currently here
  size_t line_num = 0;
  for (size_t x = 0; x < this->_line_begin_offset.size() - 1; x++)
    if (this->_line_begin_offset[line_num + 1] <= offset)
      line_num++;
  return line_num;
}

void SourceFile::_count_lines() {
  size_t last_line_start = 0;
  for (size_t x = 0; x < this->_filesize; x++) {
    if (this->_contents[x] == '\n') {
      //this->_contents[x] = 0;
      this->_line_begin_offset.push_back(last_line_start);
      last_line_start = x + 1;
    }
  }
  this->_line_begin_offset.push_back(last_line_start);
}

void SourceFile::_clear() {
  this->_filename = NULL;
  this->_contents = NULL;
  this->_line_begin_offset.clear();
  this->_filesize = 0;
}
