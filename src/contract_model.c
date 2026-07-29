#include "contract_model.h"
#include "errors.h"
#include "fact_command.h"
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_string.h>
#include <p101_c_facts/facts.h>
#include <p101_posix/p101_stdio.h>

static void apply_fact(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const struct p101_c_fact *fact);
static void add_function(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const struct p101_c_fact *fact);
static void add_event(const struct p101_env *env, struct p101_error *err, struct contract_model *model, enum contract_event_kind kind, const struct p101_c_fact *fact);
static void set_function_contract(const struct p101_env *env, struct contract_model *model, const struct p101_c_fact *fact, bool is_env);
static bool fact_line_is_complete(const struct p101_env *env, struct p101_error *err, FILE *stream, char *line);
static void copy_text(const struct p101_env *env, char *dst, size_t dst_size, const char *src);

void p101_error_contract_load_facts(const struct p101_env *env, struct p101_error *err, const struct arguments *args, struct contract_model *model)
{
    FILE *stream;
    char  command[MAX_COMMAND];
    char  line[READ_BUF_LEN];
    bool  is_pipe;

    P101_TRACE(env);
    stream  = NULL;
    is_pipe = args->facts_path == NULL;
    if(is_pipe)
    {
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
    }
    else
    {
        stream = p101_fopen(env, err, args->facts_path, "r");
    }
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

    if(is_pipe && p101_pclose(env, err, stream) != 0)
    {
        stream = NULL;
        if(p101_error_has_no_error(err))
        {
            P101_ERROR_RAISE_USER(err, "p101-wrapper-audit failed while emitting facts.", ERR_USAGE);
        }
        goto done;
    }
    if(!is_pipe)
    {
        p101_fclose(env, err, stream);
        if(p101_error_has_error(err))
        {
            stream = NULL;
            goto done;
        }
    }
    stream = NULL;

done:
    if(stream != NULL)
    {
        if(is_pipe)
        {
            (void)p101_pclose(env, NULL, stream);    // P101_ERROR_CONTRACT_ALLOW_NO_ERROR: cleanup preserves the primary error.
        }
        else
        {
            p101_fclose(env, NULL, stream);    // P101_ERROR_CONTRACT_ALLOW_NO_ERROR: cleanup preserves the primary error.
        }
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
            if(fact->flag1 || fact->flag2)
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
            else if(p101_strcmp(env, fact->value, "ERROR_OPTIONAL") == 0)
            {
                add_event(env, err, model, CONTRACT_EVENT_ERROR_OPTIONAL, fact);
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
    if(kind == CONTRACT_EVENT_CALL)
    {
        event->needs_env   = fact->flag1;
        event->needs_error = fact->flag2;
    }

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
