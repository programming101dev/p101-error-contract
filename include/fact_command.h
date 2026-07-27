#ifndef P101_ERROR_CONTRACT_FACT_COMMAND_H
#define P101_ERROR_CONTRACT_FACT_COMMAND_H

#include "arguments.h"
#include <p101_env/env.h>
#include <p101_error/error.h>

void p101_error_contract_build_fact_command(const struct p101_env *env, struct p101_error *err, char *command, size_t command_size, const struct arguments *args);

#endif    // P101_ERROR_CONTRACT_FACT_COMMAND_H
