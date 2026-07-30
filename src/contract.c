#include "contract.h"
#include "constants.h"
#include "contract_model.h"
#include "report.h"
#include <p101_c/p101_stdlib.h>
#include <p101_c/p101_string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

static void   analyze_model(const struct p101_env *env, struct p101_error *err, const struct contract_model *model, struct contract_report *report, bool strict_sequence);
static size_t next_function_line(const struct p101_env *env, const struct contract_model *model, const struct contract_function *function);
static bool   event_is_in_function(const struct p101_env *env, const struct contract_event *event, const struct contract_function *function, size_t end_line);
static bool   visible_env_before_event(const struct p101_env *env, const struct contract_model *model, const struct contract_function *function, const struct contract_event *event, size_t end_line);
static bool   visible_error_before_event(const struct p101_env *env, const struct contract_model *model, const struct contract_function *function, const struct contract_event *event, size_t end_line);
static bool   error_is_explicitly_optional(const struct p101_env *env, const struct contract_model *model, const struct contract_function *function, const struct contract_event *event, size_t end_line);
static bool   event_needs_env_contract(const struct contract_event *event);
static bool   event_needs_error_contract(const struct contract_event *event);

int p101_error_contract_run(const struct p101_env *env, struct p101_error *err, const struct arguments *args)
{
    struct contract_model *model;
    struct contract_report report;
    int                    ret_val;

    P101_TRACE_SCOPE(env);
    ret_val = EXIT_TROUBLE;
    model   = (struct contract_model *)p101_calloc(env, err, 1U, sizeof(*model));
    if(model == NULL)
    {
        goto done;
    }

    p101_error_contract_report_begin(env, err, &report, args);
    p101_error_contract_load_facts(env, err, args, model);

    if(p101_error_has_no_error(err))
    {
        report.files_scanned = model->files_scanned;
        analyze_model(env, err, model, &report, args->strict_sequence);
    }

    if(p101_error_has_no_error(err))
    {
        p101_error_contract_report_end(env, err, &report);
    }

    if(p101_error_has_error(err))
    {
        goto done;
    }

    ret_val = (report.findings == 0U) ? EXIT_SUCCESS : EXIT_FINDINGS;

done:
    p101_free(env, model);
    return ret_val;
}

static void analyze_model(const struct p101_env *env, struct p101_error *err, const struct contract_model *model, struct contract_report *report, bool strict_sequence)
{
    P101_TRACE_SCOPE(env);
    for(size_t i = 0U; i < model->function_count && p101_error_has_no_error(err); i++)
    {
        struct contract_function function;
        size_t                   end_line;

        function = model->functions[i];
        end_line = next_function_line(env, model, &function);

        for(size_t j = 0U; j < model->event_count && p101_error_has_no_error(err); j++)
        {
            const struct contract_event *event;

            event = &model->events[j];
            if(!event_is_in_function(env, event, &function, end_line))
            {
                continue;
            }

            if(event_needs_env_contract(event) && !function.env_reported && !visible_env_before_event(env, model, &function, event, end_line))
            {
                p101_error_contract_report_finding(env, err, report, "P101-ERR-001", event->path, event->line, function.name, "p101 call or P101_TRACE appears before a visible p101_env/env contract");
                function.env_reported = true;
            }

            if(event_needs_error_contract(event) && !function.error_reported && !visible_error_before_event(env, model, &function, event, end_line) && !error_is_explicitly_optional(env, model, &function, event, end_line))
            {
                p101_error_contract_report_finding(env, err, report, "P101-ERR-002", event->path, event->line, function.name, "fallible p101 call or error macro appears before a visible p101_error/err contract");
                function.error_reported = true;
            }

            if(event->kind == CONTRACT_EVENT_ERROR_DISCARD && !error_is_explicitly_optional(env, model, &function, event, end_line))
            {
                p101_error_contract_report_finding(env, err, report, "P101-ERR-003", event->path, event->line, function.name, "fallible p101 call passes NULL instead of an error object; handle the failure or document the intentional best-effort boundary");
            }

            if(strict_sequence && event->kind == CONTRACT_EVENT_ERROR_UNCHECKED_CHAIN && !error_is_explicitly_optional(env, model, &function, event, end_line))
            {
                p101_error_contract_report_finding(env, err, report, "P101-ERR-004", event->path, event->line, function.name, "another fallible p101 call is reachable before the previous error state is checked or returned");
            }
        }
    }
}

static size_t next_function_line(const struct p101_env *env, const struct contract_model *model, const struct contract_function *function)
{
    size_t line;

    P101_TRACE_SCOPE(env);
    line = (size_t)-1;
    for(size_t i = 0U; i < model->function_count; i++)
    {
        const struct contract_function *candidate;

        candidate = &model->functions[i];
        if(candidate->line <= function->line)
        {
            continue;
        }
        if(candidate->line >= line)
        {
            continue;
        }
        if(p101_strcmp(env, candidate->path, function->path) != 0)
        {
            continue;
        }
        line = candidate->line;
    }

    return line;
}

static bool event_is_in_function(const struct p101_env *env, const struct contract_event *event, const struct contract_function *function, size_t end_line)
{
    P101_TRACE_SCOPE(env);
    if(p101_strcmp(env, event->path, function->path) != 0)
    {
        return false;
    }
    if(event->line < function->line || event->line >= end_line)
    {
        return false;
    }
    return true;
}

static bool visible_env_before_event(const struct p101_env *env, const struct contract_model *model, const struct contract_function *function, const struct contract_event *event, size_t end_line)
{
    P101_TRACE_SCOPE(env);
    if(function->has_env_contract)
    {
        return true;
    }

    for(size_t i = 0U; i < model->event_count; i++)
    {
        const struct contract_event *candidate;

        candidate = &model->events[i];
        if(candidate->kind == CONTRACT_EVENT_ENV_USE && candidate->line <= event->line && event_is_in_function(env, candidate, function, end_line))
        {
            return true;
        }
    }

    return false;
}

static bool visible_error_before_event(const struct p101_env *env, const struct contract_model *model, const struct contract_function *function, const struct contract_event *event, size_t end_line)
{
    P101_TRACE_SCOPE(env);
    if(function->has_error_contract)
    {
        return true;
    }

    for(size_t i = 0U; i < model->event_count; i++)
    {
        const struct contract_event *candidate;

        candidate = &model->events[i];
        if(candidate->kind == CONTRACT_EVENT_ERROR_USE && candidate->line <= event->line && event_is_in_function(env, candidate, function, end_line))
        {
            return true;
        }
    }

    return false;
}

static bool error_is_explicitly_optional(const struct p101_env *env, const struct contract_model *model, const struct contract_function *function, const struct contract_event *event, size_t end_line)
{
    P101_TRACE_SCOPE(env);
    for(size_t i = 0U; i < model->event_count; i++)
    {
        const struct contract_event *candidate;

        candidate = &model->events[i];
        if(candidate->kind != CONTRACT_EVENT_ERROR_OPTIONAL)
        {
            continue;
        }
        if(candidate->line != event->line && candidate->line + 1U != event->line)
        {
            continue;
        }
        if(event_is_in_function(env, candidate, function, end_line))
        {
            return true;
        }
    }

    return false;
}

static bool event_needs_env_contract(const struct contract_event *event)
{
    return (event->kind == CONTRACT_EVENT_TRACE_USE || (event->kind == CONTRACT_EVENT_CALL && event->needs_env)) != 0;
}

static bool event_needs_error_contract(const struct contract_event *event)
{
    return (event->kind == CONTRACT_EVENT_ERROR_CHECK || (event->kind == CONTRACT_EVENT_CALL && event->needs_error)) != 0;
}

#ifdef P101_ERROR_CONTRACT_TESTING
void p101_error_contract_test_analyze(const struct p101_env *env, struct p101_error *err, const struct contract_model *model, struct contract_report *report)
{
    analyze_model(env, err, model, report, true);
}
#endif
