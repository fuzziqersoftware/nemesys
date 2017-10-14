#pragma once

#include "AMD64Assembler.hh" // for MemoryReference

const void* common_object_base();
MemoryReference common_object_reference(const void* which);
