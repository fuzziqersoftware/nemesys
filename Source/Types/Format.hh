#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "../Environment/Value.hh"
#include "../Compiler/Exception.hh"
#include "Strings.hh"
#include "Tuple.hh"


void bytes_typecheck_format(const std::string& format,
    const std::vector<Value>& types);
void unicode_typecheck_format(const std::wstring& format,
    const std::vector<Value>& types);

BytesObject* bytes_format(BytesObject* format, TupleObject* args,
    ExceptionBlock* exc_block = NULL);
UnicodeObject* unicode_format(UnicodeObject* format, TupleObject* args,
    ExceptionBlock* exc_block = NULL);
BytesObject* bytes_format_one(BytesObject* format, void* arg, bool is_object,
    ExceptionBlock* exc_block = NULL);
UnicodeObject* unicode_format_one(UnicodeObject* format, void* arg, bool is_object,
    ExceptionBlock* exc_block = NULL);
