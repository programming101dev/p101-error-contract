#ifndef P101_ERROR_CONTRACT_ARGUMENTS_H
#define P101_ERROR_CONTRACT_ARGUMENTS_H

#include <stdbool.h>
#include <stddef.h>

enum
{
    P101_ERROR_CONTRACT_MAX_PATHS = 64
};

struct arguments
{
    const char *fact_tool_path;
    const char *paths[P101_ERROR_CONTRACT_MAX_PATHS];
    size_t      path_count;
    bool        json;
    bool        quiet;
    bool        verbose;
};

#endif    // P101_ERROR_CONTRACT_ARGUMENTS_H
