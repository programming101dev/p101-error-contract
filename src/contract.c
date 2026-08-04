#include "contract.h"
#include "constants.h"
#include "contract_model.h"
#include "native_analysis.h"
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
static size_t function_exit_count(const struct p101_env *env, const struct contract_model *model, const struct contract_function *function, size_t end_line, size_t *second_exit_line);
static bool   exit_event_is_duplicate(const struct p101_env *env, const struct contract_model *model, size_t event_index);
static void   analyze_ownership(const struct p101_env *env, struct p101_error *err, const struct contract_model *model, struct contract_report *report);

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
        analyze_ownership(env, err, model, &report);
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

static void analyze_ownership(const struct p101_env *env, struct p101_error *err, const struct contract_model *model, struct contract_report *report)
{
    P101_TRACE_SCOPE(env);
    for(size_t i = 0U; i < model->ownership_file_count && p101_error_has_no_error(err); i++)
    {
        const struct contract_ownership_file *owner;

        owner = &model->ownership_files[i];
        if(owner->error_create_count > owner->error_destroy_count)
        {
            p101_error_contract_report_finding(env, err, report, "P101-ERR-005", owner->path, owner->first_error_create_line, "-", "this file creates more p101_error objects than it destroys; keep ownership local or make transfer explicit");
        }
        if(owner->env_create_count > owner->env_destroy_count)
        {
            p101_error_contract_report_finding(env, err, report, "P101-ERR-006", owner->path, owner->first_env_create_line, "-", "this file creates more p101_env objects than it destroys; keep ownership local or make transfer explicit");
        }
    }
}

static void analyze_model(const struct p101_env *env, struct p101_error *err, const struct contract_model *model, struct contract_report *report, bool strict_sequence)
{
    P101_TRACE_SCOPE(env);
    for(size_t i = 0U; i < model->function_count && p101_error_has_no_error(err); i++)
    {
        struct contract_function function;
        size_t                   end_line;
        size_t                   second_exit_line;

        function = model->functions[i];
        end_line = next_function_line(env, model, &function);
        if(function_exit_count(env, model, &function, end_line, &second_exit_line) > 1U)
        {
            p101_error_contract_report_finding(env,
                                               err,
                                               report,
                                               "P101-ERR-008",
                                               function.path,
                                               second_exit_line,
                                               function.name,
                                               "this function has more than one exit point; converge control flow on one final return or, for main, one final process-status decision");
        }

        for(size_t j = 0U; j < model->event_count && p101_error_has_no_error(err); j++)
        {
            const struct contract_event *event;

            event = &model->events[j];
            if(!event_is_in_function(env, event, &function, end_line))
            {
                continue;
            }

            if(event->kind == CONTRACT_EVENT_PROCESS_TERMINATION && p101_strcmp(env, function.name, "main") != 0 && !p101_error_contract_is_termination_adapter(env, function.name, event->name))
            {
                p101_error_contract_report_finding(env, err, report, "P101-ERR-007", event->path, event->line, function.name, "only main may terminate the process; return a status or raise an error so the caller controls shutdown");
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

static size_t function_exit_count(const struct p101_env *env, const struct contract_model *model, const struct contract_function *function, size_t end_line, size_t *second_exit_line)
{
    size_t count;

    P101_TRACE_SCOPE(env);
    count             = 0U;
    *second_exit_line = function->line;
    for(size_t index = 0U; index < model->event_count; index++)
    {
        const struct contract_event *event;

        event = &model->events[index];
        if(event->kind != CONTRACT_EVENT_FUNCTION_RETURN && event->kind != CONTRACT_EVENT_PROCESS_TERMINATION)
        {
            continue;
        }
        if(!event_is_in_function(env, event, function, end_line) || exit_event_is_duplicate(env, model, index))
        {
            continue;
        }
        count++;
        if(count == 2U)
        {
            *second_exit_line = event->line;
        }
    }
    return count;
}

static bool exit_event_is_duplicate(const struct p101_env *env, const struct contract_model *model, size_t event_index)
{
    const struct contract_event *event;
    bool                         duplicate;

    P101_TRACE_SCOPE(env);
    event     = &model->events[event_index];
    duplicate = false;
    for(size_t index = 0U; index < event_index; index++)
    {
        const struct contract_event *candidate;

        candidate = &model->events[index];
        if(candidate->kind == event->kind && candidate->line == event->line && candidate->start == event->start && p101_strcmp(env, candidate->path, event->path) == 0 && p101_strcmp(env, candidate->caller, event->caller) == 0)
        {
            duplicate = true;
            break;
        }
    }
    return duplicate;
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
    bool result;

    P101_TRACE_SCOPE(env);
    result = p101_strcmp(env, event->path, function->path) == 0;
    if(result && event->caller[0] != '\0')
    {
        result = p101_strcmp(env, event->caller, function->name) == 0;
    }
    else if(result && event->start != 0U && function->end > function->start)
    {
        result = (event->start >= function->start && event->start < function->end) != 0;
    }
    else if(result)
    {
        result = (event->line >= function->line && event->line < end_line) != 0;
    }
    return result;
}

static bool visible_env_before_event(const struct p101_env *env, const struct contract_model *model, const struct contract_function *function, const struct contract_event *event, size_t end_line)
{
    bool visible;

    P101_TRACE_SCOPE(env);
    visible = function->has_env_contract;
    for(size_t i = 0U; i < model->event_count && !visible; i++)
    {
        const struct contract_event *candidate;

        candidate = &model->events[i];
        if(candidate->kind == CONTRACT_EVENT_ENV_USE && candidate->line <= event->line && event_is_in_function(env, candidate, function, end_line))
        {
            visible = true;
        }
    }

    return visible;
}

static bool visible_error_before_event(const struct p101_env *env, const struct contract_model *model, const struct contract_function *function, const struct contract_event *event, size_t end_line)
{
    bool visible;

    P101_TRACE_SCOPE(env);
    visible = function->has_error_contract;
    for(size_t i = 0U; i < model->event_count && !visible; i++)
    {
        const struct contract_event *candidate;

        candidate = &model->events[i];
        if(candidate->kind == CONTRACT_EVENT_ERROR_USE && candidate->line <= event->line && event_is_in_function(env, candidate, function, end_line))
        {
            visible = true;
        }
    }

    return visible;
}

static bool error_is_explicitly_optional(const struct p101_env *env, const struct contract_model *model, const struct contract_function *function, const struct contract_event *event, size_t end_line)
{
    bool optional;

    P101_TRACE_SCOPE(env);
    optional = false;
    for(size_t i = 0U; i < model->event_count && !optional; i++)
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
            optional = true;
        }
    }

    return optional;
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
    analyze_ownership(env, err, model, report);
    analyze_model(env, err, model, report, true);
}
#endif
