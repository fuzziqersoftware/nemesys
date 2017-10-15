#pragma once

#include "AMD64Assembler.hh" // for MemoryReference

const void* common_object_base();
size_t common_object_count();
MemoryReference common_object_reference(const void* which);
