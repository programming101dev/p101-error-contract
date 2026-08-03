#include "native_analysis.h"
#include "contract_builder.h"
#include "errors.h"
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_string.h>
#include <p101_c_facts/analysis.h>
#include <p101_c_facts/project.h>

static bool apply_record(const struct p101_env *env, struct p101_error *err, const struct p101_c_analysis_record *record, void *context);
static void apply_note(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const struct p101_c_analysis_record *record);
static bool note_kind(const struct p101_env *env, const char *name, enum contract_event_kind *kind);

void p101_error_contract_load_analysis(const struct p101_env *env, struct p101_error *err, const struct arguments *args, struct contract_model *model)
{
    static const char              default_path[] = DEFAULT_SOURCE_PATH;
    const char                    *paths[P101_ERROR_CONTRACT_MAX_PATHS];
    size_t                         path_count;
    char                           discovered_compile_db[CONTRACT_PATH_LEN];
    const char                    *compile_db;
    struct p101_c_analysis_options options;

    P101_TRACE_SCOPE(env);
    path_count = args->path_count;
    if(path_count == 0U)
    {
        paths[0]   = default_path;
        path_count = 1U;
    }
    else
    {
        for(size_t index = 0U; index < path_count; index++)
        {
            paths[index] = args->paths[index];
        }
    }
    compile_db = args->compile_db_path;
    if(compile_db == NULL && p101_c_facts_find_clang_compile_database(env, err, ".", discovered_compile_db, sizeof(discovered_compile_db)))
    {
        compile_db = discovered_compile_db;
    }
    if(p101_error_has_error(err))
    {
        return;
    }
    p101_memset(env, &options, 0, sizeof(options));
    options.compile_database                     = compile_db;
    options.paths                                = paths;
    options.path_count                           = path_count;
    options.compile_database_only                = compile_db != NULL;
    options.detailed_preprocessing               = true;
    options.include_headers_as_translation_units = false;
    options.keep_going                           = false;
    if(args->verbose)
    {
        p101_fprintf(env, err, stderr, "p101-error-contract: native lib_c_facts scan (%zu path%s%s)\n", path_count, path_count == 1U ? "" : "s", compile_db == NULL ? "" : ", compile database active");
    }
    if(p101_error_has_no_error(err))
    {
        (void)p101_c_analysis_scan(env, err, &options, apply_record, model);
    }
}

static bool apply_record(const struct p101_env *env, struct p101_error *err, const struct p101_c_analysis_record *record, void *context)
{
    struct contract_model *model;

    P101_TRACE_SCOPE(env);
    model = (struct contract_model *)context;
    if(record->kind == P101_C_ANALYSIS_FILE)
    {
        model->files_scanned++;
    }
    else if(record->kind == P101_C_ANALYSIS_FUNCTION && record->is_definition)
    {
        p101_contract_model_add_function(env, err, model, record->path, record->name, record->line, record->start_offset, record->end_offset, "Too many functions in native analysis.");
    }
    else if(record->kind == P101_C_ANALYSIS_CALL)
    {
        p101_contract_model_record_ownership(env, err, model, record->path, record->line, record->name);
        if(record->has_env_parameter || record->has_error_parameter)
        {
            p101_contract_model_add_event(env,
                                          err,
                                          model,
                                          CONTRACT_EVENT_CALL,
                                          record->path,
                                          record->name,
                                          record->caller,
                                          record->line,
                                          record->start_offset,
                                          record->end_offset,
                                          record->has_env_parameter,
                                          record->has_error_parameter,
                                          "Too many events in native analysis.");
        }
    }
    else if(record->kind == P101_C_ANALYSIS_NOTE)
    {
        apply_note(env, err, model, record);
    }
    else if(record->kind == P101_C_ANALYSIS_DIAGNOSTIC)
    {
        P101_ERROR_RAISE_USER(err, record->name == NULL ? "lib_c_facts could not parse an admitted source file." : record->name, ERR_USAGE);
    }
    return p101_error_has_no_error(err);
}

static void apply_note(const struct p101_env *env, struct p101_error *err, struct contract_model *model, const struct p101_c_analysis_record *record)
{
    enum contract_event_kind kind;

    if(p101_strcmp(env, record->name, "ENV_CONTRACT") == 0)
    {
        p101_contract_model_set_contract(env, model, record->path, record->line, true);
    }
    else if(p101_strcmp(env, record->name, "ERROR_CONTRACT") == 0)
    {
        p101_contract_model_set_contract(env, model, record->path, record->line, false);
    }
    else if(note_kind(env, record->name, &kind))
    {
        p101_contract_model_add_event(env, err, model, kind, record->path, record->name, record->caller, record->line, record->start_offset, record->end_offset, false, false, "Too many events in native analysis.");
    }
}

static bool note_kind(const struct p101_env *env, const char *name, enum contract_event_kind *kind)
{
    static const struct
    {
        const char              *name;
        enum contract_event_kind kind;
    } mappings[] = {
        {"ENV_USE",               CONTRACT_EVENT_ENV_USE              },
        {"ERROR_USE",             CONTRACT_EVENT_ERROR_USE            },
        {"TRACE_USE",             CONTRACT_EVENT_TRACE_USE            },
        {"ERROR_CHECK",           CONTRACT_EVENT_ERROR_CHECK          },
        {"ERROR_OPTIONAL",        CONTRACT_EVENT_ERROR_OPTIONAL       },
        {"ERROR_DISCARD",         CONTRACT_EVENT_ERROR_DISCARD        },
        {"ERROR_PROPAGATED",      CONTRACT_EVENT_ERROR_PROPAGATED     },
        {"ERROR_UNCHECKED_CHAIN", CONTRACT_EVENT_ERROR_UNCHECKED_CHAIN},
    };

    for(size_t index = 0U; index < sizeof(mappings) / sizeof(mappings[0]); index++)
    {
        if(p101_strcmp(env, name, mappings[index].name) == 0)
        {
            *kind = mappings[index].kind;
            return true;
        }
    }
    return false;
}
