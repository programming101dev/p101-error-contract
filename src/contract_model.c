#include "contract_model.h"
#include "contract_builder.h"
#include "errors.h"
#include "native_analysis.h"
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_string.h>
#include <p101_c_facts/facts.h>

static void apply_fact(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const struct p101_c_fact *fact);
static void add_function(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const struct p101_c_fact *fact);
static void add_event(const struct p101_env *env, struct p101_error *err, struct contract_model *model, enum contract_event_kind kind, const struct p101_c_fact *fact);
static void set_function_contract(const struct p101_env *env, struct contract_model *model, const struct p101_c_fact *fact, bool is_env);
static void record_ownership_call(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const struct p101_c_fact *fact);
static bool fact_line_is_complete(const struct p101_env *env, struct p101_error *err, FILE *stream, char *line);

void p101_error_contract_load_facts(const struct p101_env *env, struct p101_error *err, const struct arguments *args, struct contract_model *model)
{
    FILE  *stream;
    char   line[READ_BUF_LEN];
    size_t fact_count;

    P101_TRACE_SCOPE(env);
    if(args->facts_path == NULL)
    {
        p101_error_contract_load_analysis(env, err, args, model);
    }
    else
    {
        stream     = p101_fopen(env, err, args->facts_path, "r");
        fact_count = 0U;

        while(stream != NULL && p101_fgets(env, err, line, sizeof(line), stream) != NULL)
        {
            struct p101_c_fact      fact;
            enum p101_c_fact_status status;

            if(fact_line_is_complete(env, err, stream, line))
            {
                status = p101_c_fact_parse_line(env, err, line, &fact);
                if(status != P101_C_FACT_OTHER)
                {
                    if(status != P101_C_FACT_OK)
                    {
                        P101_ERROR_RAISE_USER(err, "The saved fact stream contains an invalid record.", ERR_USAGE);
                        break;
                    }

                    apply_fact(env, err, model, &fact);
                    fact_count++;
                }
            }
        }

        if(stream != NULL)
        {
            if(p101_error_has_error(err))
            {
                p101_fclose(env, NULL, stream);    // P101_ERROR_CONTRACT_ALLOW_NO_ERROR: cleanup preserves the fact-loading error.
            }
            else
            {
                p101_fclose(env, err, stream);
            }
        }
        if(p101_error_has_no_error(err) && fact_count == 0U)
        {
            P101_ERROR_RAISE_USER(err, "The fact stream did not contain any p101 C facts.", ERR_USAGE);
        }
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
        case P101_C_FACT_KIND_ENUM:
        case P101_C_FACT_KIND_ENUMERATOR:
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
            if(p101_error_contract_is_process_termination_call(env, fact->value))
            {
                add_event(env, err, model, CONTRACT_EVENT_PROCESS_TERMINATION, fact);
            }
            else if(fact->flag1 || fact->flag2)
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
            else if(p101_strcmp(env, fact->value, "FUNCTION_RETURN") == 0)
            {
                add_event(env, err, model, CONTRACT_EVENT_FUNCTION_RETURN, fact);
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
    P101_TRACE_SCOPE(env);
    p101_contract_model_record_ownership(env, err, model, fact->path, fact->line, fact->value);
}

static void add_function(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const struct p101_c_fact *fact)
{
    P101_TRACE_SCOPE(env);
    p101_contract_model_add_function(env, err, model, fact->path, fact->value, fact->line, 0U, 0U, "Too many functions in fact stream.");
}

static void add_event(const struct p101_env *env, struct p101_error *err, struct contract_model *model, enum contract_event_kind kind, const struct p101_c_fact *fact)
{
    size_t event_start;

    P101_TRACE_SCOPE(env);
    event_start = kind == CONTRACT_EVENT_FUNCTION_RETURN ? fact->column : 0U;
    p101_contract_model_add_event(env,
                                  err,
                                  model,
                                  kind,
                                  fact->path,
                                  fact->value,
                                  fact->caller,
                                  fact->line,
                                  event_start,
                                  0U,
                                  (kind == CONTRACT_EVENT_CALL && fact->flag1) != 0,
                                  (kind == CONTRACT_EVENT_CALL && fact->flag2) != 0,
                                  "Too many events in fact stream.");
}

static void set_function_contract(const struct p101_env *env, struct contract_model *model, const struct p101_c_fact *fact, bool is_env)
{
    p101_contract_model_set_contract(env, model, fact->path, fact->line, is_env);
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

#ifdef P101_ERROR_CONTRACT_TESTING
void p101_error_contract_test_apply_fact(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const struct p101_c_fact *fact)
{
    apply_fact(env, err, model, fact);
}

bool p101_error_contract_test_fact_line_complete(const struct p101_env *env, struct p101_error *err, FILE *stream, char *line)
{
    return fact_line_is_complete(env, err, stream, line);
}

#endif
