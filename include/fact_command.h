#ifndef P101_ERROR_CONTRACT_FACT_COMMAND_H
#define P101_ERROR_CONTRACT_FACT_COMMAND_H

#include "arguments.h"
#include <p101_env/env.h>
#include <p101_error/error.h>
#include <p101_util/tool_run.h>

void p101_error_contract_build_fact_argv(const struct p101_env *env, struct p101_error *err, struct p101_tool_argv *command, const struct arguments *args);

#endif    // P101_ERROR_CONTRACT_FACT_COMMAND_H
