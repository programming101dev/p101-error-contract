#ifndef P101_ERROR_CONTRACT_MODEL_H
#define P101_ERROR_CONTRACT_MODEL_H

#include "arguments.h"
#include "constants.h"
#include <p101_env/env.h>
#include <p101_error/error.h>
#include <stdbool.h>
#include <stddef.h>

enum contract_event_kind
{
    CONTRACT_EVENT_CALL = 0,
    CONTRACT_EVENT_ENV_USE,
    CONTRACT_EVENT_ERROR_USE,
    CONTRACT_EVENT_TRACE_USE,
    CONTRACT_EVENT_ERROR_CHECK,
    CONTRACT_EVENT_ERROR_OPTIONAL,
    CONTRACT_EVENT_ERROR_DISCARD,
    CONTRACT_EVENT_ERROR_PROPAGATED,
    CONTRACT_EVENT_ERROR_UNCHECKED_CHAIN
};

struct contract_function
{
    char   path[CONTRACT_PATH_LEN];
    char   name[FUNCTION_NAME_LEN];
    size_t line;
    bool   has_env_contract;
    bool   has_error_contract;
    bool   env_reported;
    bool   error_reported;
};

struct contract_event
{
    enum contract_event_kind kind;
    char                     path[CONTRACT_PATH_LEN];
    char                     name[FUNCTION_NAME_LEN];
    size_t                   line;
    bool                     needs_env;
    bool                     needs_error;
};

struct contract_model
{
    struct contract_function functions[MAX_FACT_FUNCTIONS];
    struct contract_event    events[MAX_FACT_EVENTS];
    size_t                   function_count;
    size_t                   event_count;
    size_t                   files_scanned;
};

void p101_error_contract_load_facts(const struct p101_env *env, struct p101_error *err, const struct arguments *args, struct contract_model *model);

#endif    // P101_ERROR_CONTRACT_MODEL_H
