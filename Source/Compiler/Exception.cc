#include "Exception.hh"

#include <stdarg.h>

#include <phosg/Strings.hh>
#include <string>

#include "../Types/Instance.hh"
#include "../Types/Strings.hh"

using namespace std;



const size_t return_exception_block_size =
    sizeof(ExceptionBlock) + sizeof(ExceptionBlock::ExceptionBlockSpec);


void raise_python_exception_with_message(ExceptionBlock* exc_block,
    int64_t class_id, const char* message) {
  UnicodeObject* message_object = bytes_decode_ascii(message);
  void* exc = create_single_attr_instance(class_id,
      reinterpret_cast<int64_t>(message_object));

  raise_python_exception(exc_block, exc);
}

void raise_python_exception_with_format(ExceptionBlock* exc_block,
    int64_t class_id, const char* fmt, ...) {
  void* exc;

  {
    va_list va;
    va_start(va, fmt);
    string message = string_vprintf(fmt, va);
    va_end(va);

    UnicodeObject* message_object = bytes_decode_ascii(message.c_str());
    exc = create_single_attr_instance(class_id,
        reinterpret_cast<int64_t>(message_object));

    // note: it's important that message is destroyed here, not at the end of
    // the function, because raise_python_exception never returns (so it would
    // never be destroyed otherwise)
  }

  raise_python_exception(exc_block, exc);
}
