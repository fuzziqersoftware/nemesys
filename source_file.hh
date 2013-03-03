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
  const char* line(int line_num) const;
  int line_offset(int line_num) const;
  int line_end_offset(int line_num) const;
  const char* filename() const;
  int filesize() const;
  int num_lines() const;

  int line_number_of_offset(int offset) const;

private:
  char* _filename;
  char* _contents;
  int _filesize;
  vector<int> _line_begin_offset;

  void _count_lines();
  void _clear();
};

#endif // _SOURCE_FILE_HH