#ifndef P101_ERROR_CONTRACT_BUILDER_H
#define P101_ERROR_CONTRACT_BUILDER_H

#include "contract_types.h"
#include <p101_env/env.h>
#include <p101_error/error.h>

void p101_contract_model_add_function(const struct p101_env *env,
                                      struct p101_error *err,
                                      struct contract_model *model,
                                      const char *path,
                                      const char *name,
                                      size_t line,
                                      size_t start,
                                      size_t end,
                                      const char *capacity_message);
void p101_contract_model_add_event(const struct p101_env *env,
                                   struct p101_error *err,
                                   struct contract_model *model,
                                   enum contract_event_kind kind,
                                   const char *path,
                                   const char *name,
                                   const char *caller,
                                   size_t line,
                                   size_t start,
                                   size_t end,
                                   bool needs_env,
                                   bool needs_error,
                                   const char *capacity_message);
void p101_contract_model_set_contract(const struct p101_env *env, struct contract_model *model, const char *path, size_t line, bool is_env);
void p101_contract_model_record_ownership(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const char *path, size_t line, const char *name);

#endif
