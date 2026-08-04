#include "contract_builder.h"
#include "errors.h"
#include <p101_c/p101_string.h>

static void copy_text(const struct p101_env *env, char *dst, size_t dst_size, const char *src);

void p101_contract_model_add_function(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const char *path, const char *name, size_t line, size_t start, size_t end, const char *capacity_message)
{
    struct contract_function *function;

    P101_TRACE_SCOPE(env);
    if(model->function_count >= MAX_FACT_FUNCTIONS)
    {
        P101_ERROR_RAISE_USER(err, capacity_message, ERR_TOOL);
        return;
    }
    function = &model->functions[model->function_count++];
    copy_text(env, function->path, sizeof(function->path), path);
    copy_text(env, function->name, sizeof(function->name), name);
    function->line  = line;
    function->start = start;
    function->end   = end;
}

void p101_contract_model_add_event(const struct p101_env *env, struct p101_error *err, struct contract_model *model, enum contract_event_kind kind, const char *path, const char *name, const char *caller, size_t line, size_t start, size_t end, bool needs_env,
                                   bool needs_error, const char *capacity_message)
{
    struct contract_event *event;

    P101_TRACE_SCOPE(env);
    if(model->event_count >= MAX_FACT_EVENTS)
    {
        P101_ERROR_RAISE_USER(err, capacity_message, ERR_TOOL);
        return;
    }
    event = &model->events[model->event_count++];
    copy_text(env, event->path, sizeof(event->path), path);
    copy_text(env, event->name, sizeof(event->name), name);
    copy_text(env, event->caller, sizeof(event->caller), caller);
    event->line        = line;
    event->start       = start;
    event->end         = end;
    event->kind        = kind;
    event->needs_env   = needs_env;
    event->needs_error = needs_error;
}

void p101_contract_model_set_contract(const struct p101_env *env, struct contract_model *model, const char *path, size_t line, bool is_env)
{
    P101_TRACE_SCOPE(env);
    for(size_t index = 0U; index < model->function_count; index++)
    {
        struct contract_function *function;

        function = &model->functions[index];
        if(function->line != line || p101_strcmp(env, function->path, path) != 0)
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
        return;
    }
}

void p101_contract_model_record_ownership(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const char *path, size_t line, const char *name)
{
    struct contract_ownership_file *owner;
    bool                            relevant;

    relevant = (p101_strcmp(env, name, "p101_error_create") == 0 || p101_strcmp(env, name, "p101_error_destroy") == 0 || p101_strcmp(env, name, "p101_env_create") == 0 || p101_strcmp(env, name, "p101_env_destroy") == 0) != 0;
    owner    = NULL;
    for(size_t index = 0U; relevant && index < model->ownership_file_count; index++)
    {
        if(p101_strcmp(env, model->ownership_files[index].path, path) == 0)
        {
            owner = &model->ownership_files[index];
            break;
        }
    }
    if(relevant && owner == NULL)
    {
        if(model->ownership_file_count >= MAX_FACT_FUNCTIONS)
        {
            P101_ERROR_RAISE_USER(err, "Too many ownership files in the admitted source.", ERR_TOOL);
        }
        else
        {
            owner = &model->ownership_files[model->ownership_file_count++];
            copy_text(env, owner->path, sizeof(owner->path), path);
        }
    }
    if(owner != NULL && p101_strcmp(env, name, "p101_error_create") == 0)
    {
        owner->error_create_count++;
        if(owner->first_error_create_line == 0U)
        {
            owner->first_error_create_line = line;
        }
    }
    else if(owner != NULL && p101_strcmp(env, name, "p101_error_destroy") == 0)
    {
        owner->error_destroy_count++;
    }
    else if(owner != NULL && p101_strcmp(env, name, "p101_env_create") == 0)
    {
        owner->env_create_count++;
        if(owner->first_env_create_line == 0U)
        {
            owner->first_env_create_line = line;
        }
    }
    else if(owner != NULL)
    {
        owner->env_destroy_count++;
    }
}

static void copy_text(const struct p101_env *env, char *dst, size_t dst_size, const char *src)
{
    P101_TRACE_SCOPE(env);
    if(dst_size != 0U)
    {
        if(src == NULL)
        {
            dst[0] = '\0';
        }
        else
        {
            p101_strncpy(env, dst, src, dst_size - 1U);
            dst[dst_size - 1U] = '\0';
        }
    }
}

#ifdef P101_ERROR_CONTRACT_TESTING
void p101_error_contract_test_copy_text(const struct p101_env *env, char *dst, size_t dst_size, const char *src)
{
    copy_text(env, dst, dst_size, src);
}
#endif
