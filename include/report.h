#ifndef P101_ERROR_CONTRACT_REPORT_H
#define P101_ERROR_CONTRACT_REPORT_H

#include "arguments.h"
#include <p101_env/env.h>
#include <p101_error/error.h>
#include <stdbool.h>
#include <stddef.h>

struct contract_report
{
    bool   json;
    bool   quiet;
    bool   first_json_finding;
    size_t findings;
    size_t files_scanned;
};

void p101_error_contract_report_begin(const struct p101_env *env, struct p101_error *err, struct contract_report *report, const struct arguments *args);
void p101_error_contract_report_finding(const struct p101_env *env, struct p101_error *err, struct contract_report *report, const char *code, const char *path, size_t line, const char *function_name, const char *message);
void p101_error_contract_report_end(const struct p101_env *env, struct p101_error *err, struct contract_report *report);

#endif    // P101_ERROR_CONTRACT_REPORT_H
