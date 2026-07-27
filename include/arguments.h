#ifndef P101_ERROR_CONTRACT_ARGUMENTS_H
#define P101_ERROR_CONTRACT_ARGUMENTS_H

#include <stdbool.h>

struct arguments
{
    char *const *paths;
    int          path_count;
    bool         json;
    bool         quiet;
};

#endif    // P101_ERROR_CONTRACT_ARGUMENTS_H
