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

  const char* data();
  const char* line(int line_num);
  const char* filename();
  int filesize();
  int num_lines();

private:
  char* _filename;
  char* _contents;
  int _filesize;
  vector<int> _line_begin_offset;

  void _count_lines();
  void _clear();
};

#endif // _SOURCE_FILE_HH