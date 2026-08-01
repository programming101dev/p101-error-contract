#include "contract_model.h"
#include "errors.h"
#include "fact_command.h"
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_string.h>
#include <p101_c_facts/facts.h>
#include <p101_util/tool_run.h>
#include <sys/wait.h>

static void apply_fact(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const struct p101_c_fact *fact);
static void add_function(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const struct p101_c_fact *fact);
static void add_event(const struct p101_env *env, struct p101_error *err, struct contract_model *model, enum contract_event_kind kind, const struct p101_c_fact *fact);
static void set_function_contract(const struct p101_env *env, struct contract_model *model, const struct p101_c_fact *fact, bool is_env);
static void record_ownership_call(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const struct p101_c_fact *fact);
static bool fact_line_is_complete(const struct p101_env *env, struct p101_error *err, FILE *stream, char *line);
static void copy_text(const struct p101_env *env, char *dst, size_t dst_size, const char *src);

void p101_error_contract_load_facts(const struct p101_env *env, struct p101_error *err, const struct arguments *args, struct contract_model *model)
{
    FILE                      *stream;
    char                       line[READ_BUF_LEN];
    bool                       is_pipe;
    bool                       pipe_open;
    size_t                     fact_count;
    struct p101_tool_argv      command;
    struct p101_tool_read_pipe pipe_state = {NULL, -1};

    P101_TRACE_SCOPE(env);
    stream     = NULL;
    is_pipe    = args->facts_path == NULL;
    pipe_open  = false;
    fact_count = 0U;
    if(is_pipe)
    {
        p101_error_contract_build_fact_argv(env, err, &command, args);
        if(p101_error_has_error(err))
        {
            goto done;
        }

        if(args->verbose)
        {
            p101_fputs(env, err, "p101-error-contract: fact argv:", stderr);
            for(size_t index = 0U; index < command.count && p101_error_has_no_error(err); index++)
            {
                p101_fprintf(env, err, stderr, " [%s]", command.values[index]);
            }
            p101_fputc(env, err, '\n', stderr);
            if(p101_error_has_error(err))
            {
                goto done;
            }
        }

        pipe_open = p101_tool_read_pipe_open(env, err, command.values, "p101-error-contract fact producer", true, &pipe_state);
        stream    = pipe_state.stream;
    }
    else
    {
        stream = p101_fopen(env, err, args->facts_path, "r");
    }
    if(stream == NULL)
    {
        goto done;
    }

    while(p101_fgets(env, err, line, sizeof(line), stream) != NULL)
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
            P101_ERROR_RAISE_USER(err, "p101-wrapper-audit emitted an invalid fact record.", ERR_USAGE);
            break;
        }

        apply_fact(env, err, model, &fact);
        fact_count++;
    }

    if(p101_error_has_error(err))
    {
        goto done;
    }

    if(is_pipe)
    {
        int status;

        status    = p101_tool_read_pipe_close(env, err, &pipe_state);
        stream    = NULL;
        pipe_open = false;
        if(p101_error_has_no_error(err) && (!WIFEXITED(status) || WEXITSTATUS(status) != 0))
        {
            P101_ERROR_RAISE_USER(err, "p101-wrapper-audit failed while emitting facts.", ERR_USAGE);
        }
        if(p101_error_has_error(err))
        {
            goto done;
        }
    }
    else
    {
        p101_fclose(env, err, stream);
    }
    stream = NULL;
    if(fact_count == 0U)
    {
        P101_ERROR_RAISE_USER(err, "The fact stream did not contain any p101 C facts.", ERR_USAGE);
    }

done:
    if(stream != NULL)
    {
        if(pipe_open)
        {
            (void)p101_tool_read_pipe_close(env, NULL, &pipe_state);    // P101_ERROR_CONTRACT_ALLOW_NO_ERROR: cleanup preserves the primary error.
        }
        else
        {
            p101_fclose(env, NULL, stream);    // P101_ERROR_CONTRACT_ALLOW_NO_ERROR: cleanup preserves the primary error.
        }
    }
    if(is_pipe)
    {
        p101_tool_argv_destroy(env, &command);
    }
}

static void apply_fact(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const struct p101_c_fact *fact)
{
    P101_TRACE_SCOPE(env);

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
            record_ownership_call(env, err, model, fact);
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
            else if(p101_strcmp(env, fact->value, "ERROR_DISCARD") == 0)
            {
                add_event(env, err, model, CONTRACT_EVENT_ERROR_DISCARD, fact);
            }
            else if(p101_strcmp(env, fact->value, "ERROR_PROPAGATED") == 0)
            {
                add_event(env, err, model, CONTRACT_EVENT_ERROR_PROPAGATED, fact);
            }
            else if(p101_strcmp(env, fact->value, "ERROR_UNCHECKED_CHAIN") == 0)
            {
                add_event(env, err, model, CONTRACT_EVENT_ERROR_UNCHECKED_CHAIN, fact);
            }
            break;
        default:
            break;
    }
#ifdef __clang__
    #pragma clang diagnostic pop
#endif
}

static void record_ownership_call(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const struct p101_c_fact *fact)
{
    struct contract_ownership_file *owner;
    bool                            relevant;

    relevant = (p101_strcmp(env, fact->value, "p101_error_create") == 0 || p101_strcmp(env, fact->value, "p101_error_destroy") == 0 || p101_strcmp(env, fact->value, "p101_env_create") == 0 || p101_strcmp(env, fact->value, "p101_env_destroy") == 0) != 0;
    if(!relevant)
    {
        return;
    }

    owner = NULL;
    for(size_t i = 0U; i < model->ownership_file_count; i++)
    {
        if(p101_strcmp(env, model->ownership_files[i].path, fact->path) == 0)
        {
            owner = &model->ownership_files[i];
            break;
        }
    }
    if(owner == NULL)
    {
        if(model->ownership_file_count >= MAX_FACT_FUNCTIONS)
        {
            P101_ERROR_RAISE_USER(err, "Too many ownership files in fact stream.", ERR_TOOL);
            return;
        }
        owner = &model->ownership_files[model->ownership_file_count++];
        copy_text(env, owner->path, sizeof(owner->path), fact->path);
    }

    if(p101_strcmp(env, fact->value, "p101_error_create") == 0)
    {
        owner->error_create_count++;
        if(owner->first_error_create_line == 0U)
        {
            owner->first_error_create_line = fact->line;
        }
    }
    else if(p101_strcmp(env, fact->value, "p101_error_destroy") == 0)
    {
        owner->error_destroy_count++;
    }
    else if(p101_strcmp(env, fact->value, "p101_env_create") == 0)
    {
        owner->env_create_count++;
        if(owner->first_env_create_line == 0U)
        {
            owner->first_env_create_line = fact->line;
        }
    }
    else
    {
        owner->env_destroy_count++;
    }
}

static void add_function(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const struct p101_c_fact *fact)
{
    struct contract_function *function;

    P101_TRACE_SCOPE(env);
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

    P101_TRACE_SCOPE(env);
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
    P101_TRACE_SCOPE(env);
    for(size_t i = 0U; i < model->function_count; i++)
    {
        struct contract_function *function;

        function = &model->functions[i];
        if(function->line != fact->line)
        {
            continue;
        }
        if(p101_strcmp(env, function->path, fact->path) != 0)
        {
            continue;
        }
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

static bool fact_line_is_complete(const struct p101_env *env, struct p101_error *err, FILE *stream, char *line)
{
    bool   complete;
    size_t length;

    P101_TRACE_SCOPE(env);
    complete = true;
    length   = p101_strlen(env, line);

    if(length != READ_BUF_LEN - 1U || p101_strchr(env, line, '\n') != NULL)
    {
        goto done;
    }

    {
        char discard[READ_BUF_LEN];

        complete = false;
        while(p101_fgets(env, err, discard, sizeof(discard), stream) != NULL)
        {
            if(p101_strchr(env, discard, '\n') != NULL)
            {
                break;
            }
        }
    }

done:
    return complete;
}

static void copy_text(const struct p101_env *env, char *dst, size_t dst_size, const char *src)
{
    P101_TRACE_SCOPE(env);
    if(dst_size == 0U)
    {
        goto done;
    }

    if(src == NULL)
    {
        dst[0] = '\0';
        goto done;
    }

    p101_strncpy(env, dst, src, dst_size - 1U);
    dst[dst_size - 1U] = '\0';

done:
    return;
}

#ifdef P101_ERROR_CONTRACT_TESTING
void p101_error_contract_test_apply_fact(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const struct p101_c_fact *fact)
{
    apply_fact(env, err, model, fact);
}

bool p101_error_contract_test_fact_line_complete(const struct p101_env *env, struct p101_error *err, FILE *stream, char *line)
{
    return fact_line_is_complete(env, err, stream, line);
}

void p101_error_contract_test_copy_text(const struct p101_env *env, char *dst, size_t dst_size, const char *src)
{
    copy_text(env, dst, dst_size, src);
}
#endif
