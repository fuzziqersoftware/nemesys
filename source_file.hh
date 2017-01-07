#ifndef _SOURCE_FILE_HH
#define _SOURCE_FILE_HH

#include <vector>

using namespace std;

class SourceFile {
  // class to represent source files.
  // load a source file by constructing this class:
  // s = SourceFile("file.py")
  // if loading FAILED, all of the methods on s will return NULL.

public:
  SourceFile(char* filename);
  ~SourceFile();

  const char* data() const;
  const char* line(size_t line_num) const;
  size_t line_offset(size_t line_num) const;
  size_t line_end_offset(size_t line_num) const;
  const char* filename() const;
  size_t filesize() const;
  size_t num_lines() const;

  size_t line_number_of_offset(size_t offset) const;

private:
  char* _filename;
  char* _contents;
  size_t _filesize;
  vector<size_t> _line_begin_offset;

  void _count_lines();
  void _clear();
};

#endif // _SOURCE_FILE_HH