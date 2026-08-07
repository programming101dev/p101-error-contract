#include "contract_builder.h"
#include "errors.h"
#include <p101_c/p101_string.h>

static void copy_text(const struct p101_env *env, char *dst, size_t dst_size, const char *src);
static bool identity_fits(const struct p101_env *env, const char *identity);

bool p101_contract_ownership_kind_from_role(const struct p101_env *env, const char *role, enum contract_ownership_kind *kind)
{
    static const struct
    {
        const char                  *role;
        enum contract_ownership_kind kind;
    } mappings[] = {
        {"CALLEE_SEMANTIC_ROLE:p101:ownership:error:acquire", CONTRACT_OWNERSHIP_ERROR_ACQUIRE},
        {"CALLEE_SEMANTIC_ROLE:p101:ownership:error:release", CONTRACT_OWNERSHIP_ERROR_RELEASE},
        {"CALLEE_SEMANTIC_ROLE:p101:ownership:env:acquire",   CONTRACT_OWNERSHIP_ENV_ACQUIRE  },
        {"CALLEE_SEMANTIC_ROLE:p101:ownership:env:release",   CONTRACT_OWNERSHIP_ENV_RELEASE  },
    };

    bool found;

    found = false;
    for(size_t index = 0U; index < sizeof(mappings) / sizeof(mappings[0]) && !found; index++)
    {
        int p101_call_result_1;

        p101_call_result_1 = p101_strcmp(env, role, mappings[index].role);
        if(p101_call_result_1 == 0)
        {
            *kind = mappings[index].kind;
            found = true;
        }
    }
    return found;
}

void p101_contract_model_add_function(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const char *path, const char *name, const char *usr, size_t line, size_t start, size_t end, const char *capacity_message)
{
    bool                      p101_call_result_2;
    struct contract_function *function;

    P101_TRACE_SCOPE(env);
    if(model->function_count >= MAX_FACT_FUNCTIONS)
    {
        P101_ERROR_RAISE_USER(err, capacity_message, ERR_TOOL);
        goto done;
    }
    p101_call_result_2 = identity_fits(env, usr);
    if(!p101_call_result_2)
    {
        P101_ERROR_RAISE_USER(err, "A resolved function identity is too long for the error-contract model.", ERR_TOOL);
        goto done;
    }
    function = &model->functions[model->function_count++];
    copy_text(env, function->path, sizeof(function->path), path);
    copy_text(env, function->name, sizeof(function->name), name);
    copy_text(env, function->usr, sizeof(function->usr), usr);
    function->line  = line;
    function->start = start;
    function->end   = end;

done:
    return;
}

void p101_contract_model_add_event(const struct p101_env *env, struct p101_error *err, struct contract_model *model, enum contract_event_kind kind, const char *path, const char *name, const char *caller, const char *usr, const char *caller_usr, size_t line,
                                   size_t start, size_t end, bool needs_env, bool needs_error, const char *capacity_message)
{
    int                    p101_expression_result_4;
    bool                   p101_call_result_5;
    bool                   p101_call_result_6;
    struct contract_event *event;

    P101_TRACE_SCOPE(env);
    if(model->event_count >= MAX_FACT_EVENTS)
    {
        P101_ERROR_RAISE_USER(err, capacity_message, ERR_TOOL);
        goto done;
    }
    p101_call_result_5 = identity_fits(env, usr);
    if(!p101_call_result_5)
    {
        p101_expression_result_4 = 1;
    }
    else
    {
        p101_call_result_6 = identity_fits(env, caller_usr);
        if(!p101_call_result_6)
        {
            p101_expression_result_4 = 1;
        }
        else
        {
            p101_expression_result_4 = 0;
        }
    }
    if(p101_expression_result_4)
    {
        P101_ERROR_RAISE_USER(err, "A resolved call identity is too long for the error-contract model.", ERR_TOOL);
        goto done;
    }
    event = &model->events[model->event_count++];
    copy_text(env, event->path, sizeof(event->path), path);
    copy_text(env, event->name, sizeof(event->name), name);
    copy_text(env, event->caller, sizeof(event->caller), caller);
    copy_text(env, event->usr, sizeof(event->usr), usr);
    copy_text(env, event->caller_usr, sizeof(event->caller_usr), caller_usr);
    event->line        = line;
    event->start       = start;
    event->end         = end;
    event->kind        = kind;
    event->needs_env   = needs_env;
    event->needs_error = needs_error;

done:
    return;
}

void p101_contract_model_set_contract(const struct p101_env *env, struct contract_model *model, const char *path, const char *function_usr, bool is_env)
{
    int p101_expression_result_7;
    int p101_expression_result_8;
    int p101_expression_result_9;
    int p101_call_result_10;
    int p101_call_result_11;
    P101_TRACE_SCOPE(env);
    for(size_t index = 0U; index < model->function_count; index++)
    {
        struct contract_function *function;

        function = &model->functions[index];
        if(function_usr == NULL)
        {
            p101_expression_result_9 = 1;
        }
        else
        {
            if(function_usr[0] == '\0')
            {
                p101_expression_result_9 = 1;
            }
            else
            {
                p101_expression_result_9 = 0;
            }
        }
        if(p101_expression_result_9)
        {
            p101_expression_result_8 = 1;
        }
        else
        {
            p101_call_result_10 = p101_strcmp(env, function->usr, function_usr);
            if(p101_call_result_10 != 0)
            {
                p101_expression_result_8 = 1;
            }
            else
            {
                p101_expression_result_8 = 0;
            }
        }
        if(p101_expression_result_8)
        {
            p101_expression_result_7 = 1;
        }
        else
        {
            p101_call_result_11 = p101_strcmp(env, function->path, path);
            if(p101_call_result_11 != 0)
            {
                p101_expression_result_7 = 1;
            }
            else
            {
                p101_expression_result_7 = 0;
            }
        }
        if(p101_expression_result_7)
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

void p101_contract_model_set_termination_adapter(const struct p101_env *env, struct contract_model *model, const char *path, const char *function_usr)
{
    int p101_call_result_15;
    int p101_call_result_16;
    P101_TRACE_SCOPE(env);
    for(size_t index = 0U; index < model->function_count; index++)
    {
        int                       p101_expression_result_12;
        int                       p101_expression_result_13;
        int                       p101_expression_result_14;
        struct contract_function *function;

        function                  = &model->functions[index];
        p101_expression_result_14 = 0;
        if(function_usr != NULL)
        {
            if(function_usr[0] != '\0')
            {
                p101_expression_result_14 = 1;
            }
        }
        p101_expression_result_13 = 0;
        if(p101_expression_result_14)
        {
            p101_call_result_15 = p101_strcmp(env, function->usr, function_usr);
            if(p101_call_result_15 == 0)
            {
                p101_expression_result_13 = 1;
            }
        }
        p101_expression_result_12 = 0;
        if(p101_expression_result_13)
        {
            p101_call_result_16 = p101_strcmp(env, function->path, path);
            if(p101_call_result_16 == 0)
            {
                p101_expression_result_12 = 1;
            }
        }
        if(p101_expression_result_12)
        {
            function->is_termination_adapter = true;
            break;
        }
    }
}

void p101_contract_model_record_ownership(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const char *path, size_t line, enum contract_ownership_kind kind)
{
    struct contract_ownership_file *owner;

    owner = NULL;
    for(size_t index = 0U; index < model->ownership_file_count; index++)
    {
        int p101_call_result_3;

        p101_call_result_3 = p101_strcmp(env, model->ownership_files[index].path, path);
        if(p101_call_result_3 == 0)
        {
            owner = &model->ownership_files[index];
            break;
        }
    }
    if(owner == NULL)
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
    if(owner != NULL && kind == CONTRACT_OWNERSHIP_ERROR_ACQUIRE)
    {
        owner->error_create_count++;
        if(owner->first_error_create_line == 0U)
        {
            owner->first_error_create_line = line;
        }
    }
    else if(owner != NULL && kind == CONTRACT_OWNERSHIP_ERROR_RELEASE)
    {
        owner->error_destroy_count++;
    }
    else if(owner != NULL && kind == CONTRACT_OWNERSHIP_ENV_ACQUIRE)
    {
        owner->env_create_count++;
        if(owner->first_env_create_line == 0U)
        {
            owner->first_env_create_line = line;
        }
    }
    else if(owner != NULL && kind == CONTRACT_OWNERSHIP_ENV_RELEASE)
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

static bool identity_fits(const struct p101_env *env, const char *identity)
{
    bool result;

    result = true;
    if(identity != NULL)
    {
        size_t identity_length;

        identity_length = p101_strlen(env, identity);
        result          = identity_length < FUNCTION_NAME_LEN;
    }
    return result;
}

#ifdef P101_ERROR_CONTRACT_TESTING
void p101_error_contract_test_copy_text(const struct p101_env *env, char *dst, size_t dst_size, const char *src)
{
    copy_text(env, dst, dst_size, src);
}
#endif
