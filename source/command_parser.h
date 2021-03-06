#ifndef __CMD_PARSER_H_
#define __CMD_PARSER_H_

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <regex>
#include <sstream>
#include "memo.h"
#include "Trace.h"

typedef enum struct PARSE_ERROR
{
    NONE=0,
    NEED_MORE_DATA=1,
    INVALID_COMMAND=2,
    QUIT=3
};

PARSE_ERROR parse_command(std::string&, char*& res_str, size_t* res_len);


#endif // __CMD_PARSER_H_
