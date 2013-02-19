#include <stdio.h>

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

const char* SourceFile::data() {
  return this->_contents;
}

const char* SourceFile::line(int line_num) {
  if (line_num < 0 || line_num > this->_line_begin_offset.size())
    return NULL;
  return &this->_contents[this->_line_begin_offset[line_num]];
}

const char* SourceFile::filename() {
  return this->_filename;
}

int SourceFile::filesize() {
  return this->_filesize;
}

int SourceFile::num_lines() {
  return this->_line_begin_offset.size();
}

void SourceFile::_count_lines() {
  int line_num = 1;
  int last_line_start = 0;
  for (int x = 0; x < this->_filesize; x++) {
    if (this->_contents[x] == '\n') {
      this->_contents[x] = 0;
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
