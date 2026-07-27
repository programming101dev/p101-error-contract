#include "contract.h"
#include "constants.h"
#include "errors.h"
#include "fact_command.h"
#include "report.h"
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_stdlib.h>
#include <p101_c/p101_string.h>
#include <p101_c_facts/facts.h>
#include <p101_posix/p101_stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

enum contract_event_kind
{
    CONTRACT_EVENT_CALL = 0,
    CONTRACT_EVENT_ENV_USE,
    CONTRACT_EVENT_ERROR_USE,
    CONTRACT_EVENT_TRACE_USE,
    CONTRACT_EVENT_ERROR_CHECK
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
};

struct contract_model
{
    struct contract_function functions[MAX_FACT_FUNCTIONS];
    struct contract_event    events[MAX_FACT_EVENTS];
    size_t                   function_count;
    size_t                   event_count;
    size_t                   files_scanned;
};

static void   load_facts(const struct p101_env *env, struct p101_error *err, const struct arguments *args, struct contract_model *model);
static void   apply_fact(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const struct p101_c_fact *fact);
static void   add_function(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const struct p101_c_fact *fact);
static void   add_event(const struct p101_env *env, struct p101_error *err, struct contract_model *model, enum contract_event_kind kind, const struct p101_c_fact *fact);
static void   set_function_contract(const struct p101_env *env, struct contract_model *model, const struct p101_c_fact *fact, bool is_env);
static void   analyze_model(const struct p101_env *env, struct p101_error *err, const struct contract_model *model, struct contract_report *report);
static size_t next_function_line(const struct p101_env *env, const struct contract_model *model, const struct contract_function *function);
static bool   event_is_in_function(const struct p101_env *env, const struct contract_event *event, const struct contract_function *function, size_t end_line);
static bool   visible_env_before_event(const struct p101_env *env, const struct contract_model *model, const struct contract_function *function, const struct contract_event *event, size_t end_line);
static bool   visible_error_before_event(const struct p101_env *env, const struct contract_model *model, const struct contract_function *function, const struct contract_event *event, size_t end_line);
static bool   call_needs_env(const struct p101_env *env, const char *name);
static bool   call_needs_error(const struct p101_env *env, const char *name);
static bool   name_is_in_list(const struct p101_env *env, const char *name, const char *const names[]);
static bool   fact_line_is_complete(const struct p101_env *env, struct p101_error *err, FILE *stream, char *line);
static void   copy_text(const struct p101_env *env, char *dst, size_t dst_size, const char *src);

int p101_error_contract_run(const struct p101_env *env, struct p101_error *err, const struct arguments *args)
{
    struct contract_model *model;
    struct contract_report report;
    int                    ret_val;

    P101_TRACE(env);
    ret_val = EXIT_TROUBLE;
    model   = (struct contract_model *)p101_calloc(env, err, 1U, sizeof(*model));
    if(model == NULL || p101_error_has_error(err))
    {
        goto done;
    }

    p101_error_contract_report_begin(env, err, &report, args);
    load_facts(env, err, args, model);

    if(p101_error_has_no_error(err))
    {
        report.files_scanned = model->files_scanned;
        analyze_model(env, err, model, &report);
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

static void load_facts(const struct p101_env *env, struct p101_error *err, const struct arguments *args, struct contract_model *model)
{
    FILE *stream;
    char  command[MAX_COMMAND];
    char  line[READ_BUF_LEN];

    P101_TRACE(env);
    stream = NULL;
    p101_error_contract_build_fact_command(env, err, command, sizeof(command), args);
    if(p101_error_has_error(err))
    {
        goto done;
    }

    if(args->verbose)
    {
        p101_fprintf(env, err, stderr, "p101-error-contract: fact command: %s\n", command);
        if(p101_error_has_error(err))
        {
            goto done;
        }
    }

    stream = p101_popen(env, err, command, "r");
    if(stream == NULL)
    {
        goto done;
    }

    while(p101_fgets(env, err, line, sizeof(line), stream) != NULL && p101_error_has_no_error(err))
    {
        struct p101_c_fact      fact;
        enum p101_c_fact_status status;

        if(!fact_line_is_complete(env, err, stream, line))
        {
            continue;
        }

        status = p101_c_fact_parse_line(env, err, line, &fact);
        if(status == P101_C_FACT_OTHER)
        {
            continue;
        }
        if(status != P101_C_FACT_OK)
        {
            if(p101_error_has_no_error(err))
            {
                P101_ERROR_RAISE_USER(err, "p101-wrapper-audit emitted an invalid fact record.", ERR_USAGE);
            }
            break;
        }

        apply_fact(env, err, model, &fact);
    }

    if(p101_error_has_error(err))
    {
        goto done;
    }

    if(p101_pclose(env, err, stream) != 0)
    {
        stream = NULL;
        if(p101_error_has_no_error(err))
        {
            P101_ERROR_RAISE_USER(err, "p101-wrapper-audit failed while emitting facts.", ERR_USAGE);
        }
        goto done;
    }
    stream = NULL;

done:
    if(stream != NULL)
    {
        (void)p101_pclose(env, NULL, stream);
    }
}

static void apply_fact(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const struct p101_c_fact *fact)
{
    P101_TRACE(env);

#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wcovered-switch-default"
#endif
    switch(fact->kind)
    {
        case P101_C_FACT_KIND_UNKNOWN:
            break;
        case P101_C_FACT_KIND_FILE:
            model->files_scanned++;
            break;
        case P101_C_FACT_KIND_INCLUDE:
        case P101_C_FACT_KIND_TYPE:
        case P101_C_FACT_KIND_MACRO:
            break;
        case P101_C_FACT_KIND_FUNCTION:
            if(!fact->flag2)
            {
                add_function(env, err, model, fact);
            }
            break;
        case P101_C_FACT_KIND_CALL:
            if(call_needs_env(env, fact->value) || call_needs_error(env, fact->value))
            {
                add_event(env, err, model, CONTRACT_EVENT_CALL, fact);
            }
            break;
        case P101_C_FACT_KIND_NOTE:
            if(p101_strcmp(env, fact->value, "ENV_CONTRACT") == 0)
            {
                set_function_contract(env, model, fact, true);
            }
            else if(p101_strcmp(env, fact->value, "ERROR_CONTRACT") == 0)
            {
                set_function_contract(env, model, fact, false);
            }
            else if(p101_strcmp(env, fact->value, "ENV_USE") == 0)
            {
                add_event(env, err, model, CONTRACT_EVENT_ENV_USE, fact);
            }
            else if(p101_strcmp(env, fact->value, "ERROR_USE") == 0)
            {
                add_event(env, err, model, CONTRACT_EVENT_ERROR_USE, fact);
            }
            else if(p101_strcmp(env, fact->value, "TRACE_USE") == 0)
            {
                add_event(env, err, model, CONTRACT_EVENT_TRACE_USE, fact);
            }
            else if(p101_strcmp(env, fact->value, "ERROR_CHECK") == 0)
            {
                add_event(env, err, model, CONTRACT_EVENT_ERROR_CHECK, fact);
            }
            break;
        default:
            break;
    }
#ifdef __clang__
    #pragma clang diagnostic pop
#endif
}

static void add_function(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const struct p101_c_fact *fact)
{
    struct contract_function *function;

    P101_TRACE(env);
    if(model->function_count >= MAX_FACT_FUNCTIONS)
    {
        P101_ERROR_RAISE_USER(err, "Too many functions in fact stream.", ERR_TOOL);
        goto done;
    }

    function = &model->functions[model->function_count++];
    copy_text(env, function->path, sizeof(function->path), fact->path);
    copy_text(env, function->name, sizeof(function->name), fact->value);
    function->line = fact->line;

done:
    return;
}

static void add_event(const struct p101_env *env, struct p101_error *err, struct contract_model *model, enum contract_event_kind kind, const struct p101_c_fact *fact)
{
    struct contract_event *event;

    P101_TRACE(env);
    if(model->event_count >= MAX_FACT_EVENTS)
    {
        P101_ERROR_RAISE_USER(err, "Too many events in fact stream.", ERR_TOOL);
        goto done;
    }

    event = &model->events[model->event_count++];
    copy_text(env, event->path, sizeof(event->path), fact->path);
    copy_text(env, event->name, sizeof(event->name), fact->value);
    event->line = fact->line;
    event->kind = kind;

done:
    return;
}

static void set_function_contract(const struct p101_env *env, struct contract_model *model, const struct p101_c_fact *fact, bool is_env)
{
    P101_TRACE(env);
    for(size_t i = 0U; i < model->function_count; i++)
    {
        struct contract_function *function;

        function = &model->functions[i];
        if(function->line == fact->line && p101_strcmp(env, function->path, fact->path) == 0)
        {
            if(is_env)
            {
                function->has_env_contract = true;
            }
            else
            {
                function->has_error_contract = true;
            }
            break;
        }
    }
}

static void analyze_model(const struct p101_env *env, struct p101_error *err, const struct contract_model *model, struct contract_report *report)
{
    P101_TRACE(env);
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

            if((event->kind == CONTRACT_EVENT_TRACE_USE || (event->kind == CONTRACT_EVENT_CALL && call_needs_env(env, event->name))) && !function.env_reported && !visible_env_before_event(env, model, &function, event, end_line))
            {
                p101_error_contract_report_finding(env, err, report, "P101-ERR-001", event->path, event->line, function.name, "p101 call or P101_TRACE appears before a visible p101_env/env contract");
                function.env_reported = true;
            }

            if((event->kind == CONTRACT_EVENT_ERROR_CHECK || (event->kind == CONTRACT_EVENT_CALL && call_needs_error(env, event->name))) && !function.error_reported && !visible_error_before_event(env, model, &function, event, end_line))
            {
                p101_error_contract_report_finding(env, err, report, "P101-ERR-002", event->path, event->line, function.name, "fallible p101 call or error macro appears before a visible p101_error/err contract");
                function.error_reported = true;
            }
        }
    }
}

static size_t next_function_line(const struct p101_env *env, const struct contract_model *model, const struct contract_function *function)
{
    size_t line;

    P101_TRACE(env);
    line = (size_t)-1;
    for(size_t i = 0U; i < model->function_count; i++)
    {
        const struct contract_function *candidate;

        candidate = &model->functions[i];
        if(candidate->line > function->line && candidate->line < line && p101_strcmp(env, candidate->path, function->path) == 0)
        {
            line = candidate->line;
        }
    }

    return line;
}

static bool event_is_in_function(const struct p101_env *env, const struct contract_event *event, const struct contract_function *function, size_t end_line)
{
    P101_TRACE(env);
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
    P101_TRACE(env);
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
    P101_TRACE(env);
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

static bool call_needs_env(const struct p101_env *env, const char *name)
{
    static const char *const names[] = {
        "p101_access",  "p101_calloc", "p101_chdir", "p101_closedir", "p101_close", "p101_dup",    "p101_dup2",   "p101_env_destroy", "p101_execv",   "p101_execve",  "p101_execvp", "p101_fclose", "p101_fgets", "p101_fopen",
        "p101_fprintf", "p101_fputs",  "p101_fread", "p101_fstat",    "p101_lstat", "p101_malloc", "p101_memset", "p101_mkdir",       "p101_open",    "p101_opendir", "p101_pipe",   "p101_printf", "p101_read",  "p101_realloc",
        "p101_readdir", "p101_rename", "p101_rmdir", "p101_snprintf", "p101_stat",  "p101_strchr", "p101_strcmp", "p101_strlen",      "p101_strncmp", "p101_strrchr", "p101_strstr", "p101_unlink", "p101_write", NULL,
    };

    P101_TRACE(env);
    return name_is_in_list(env, name, names);
}

static bool call_needs_error(const struct p101_env *env, const char *name)
{
    static const char *const names[] = {
        "p101_access",
        "p101_calloc",
        "p101_chdir",
        "p101_closedir",
        "p101_close",
        "p101_dup",
        "p101_dup2",
        "p101_error_has_error",
        "p101_error_has_no_error",
        "p101_error_is_error",
        "p101_error_reset",
        "p101_execv",
        "p101_execve",
        "p101_execvp",
        "p101_fclose",
        "p101_fgets",
        "p101_fopen",
        "p101_fprintf",
        "p101_fputs",
        "p101_fread",
        "p101_fstat",
        "p101_lstat",
        "p101_malloc",
        "p101_mkdir",
        "p101_open",
        "p101_opendir",
        "p101_pipe",
        "p101_printf",
        "p101_read",
        "p101_realloc",
        "p101_readdir",
        "p101_rename",
        "p101_rmdir",
        "p101_snprintf",
        "p101_stat",
        "p101_unlink",
        "p101_write",
        NULL,
    };

    P101_TRACE(env);
    return name_is_in_list(env, name, names);
}

static bool name_is_in_list(const struct p101_env *env, const char *name, const char *const names[])
{
    P101_TRACE(env);
    if(name == NULL)
    {
        return false;
    }

    for(size_t i = 0U; names[i] != NULL; i++)
    {
        if(p101_strcmp(env, name, names[i]) == 0)
        {
            return true;
        }
    }

    return false;
}

static bool fact_line_is_complete(const struct p101_env *env, struct p101_error *err, FILE *stream, char *line)
{
    bool   complete;
    size_t length;

    P101_TRACE(env);
    complete = true;
    length   = p101_strlen(env, line);

    if(length == READ_BUF_LEN - 1U && p101_strchr(env, line, '\n') == NULL)
    {
        char discard[READ_BUF_LEN];

        complete = false;
        while(p101_error_has_no_error(err) && p101_fgets(env, err, discard, sizeof(discard), stream) != NULL)
        {
            if(p101_strchr(env, discard, '\n') != NULL)
            {
                break;
            }
        }
    }

    return complete;
}

static void copy_text(const struct p101_env *env, char *dst, size_t dst_size, const char *src)
{
    P101_TRACE(env);
    if(dst_size == 0U)
    {
        return;
    }

    if(src == NULL)
    {
        dst[0] = '\0';
        return;
    }

    p101_strncpy(env, dst, src, dst_size - 1U);
    dst[dst_size - 1U] = '\0';
}
