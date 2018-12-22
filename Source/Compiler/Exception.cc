#include "Exception.hh"


const size_t return_exception_block_size =
    sizeof(ExceptionBlock) + sizeof(ExceptionBlock::ExceptionBlockSpec);
