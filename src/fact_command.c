#include "fact_command.h"
#include "constants.h"
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_stdlib.h>
#include <p101_c_facts/project.h>

static void        append_include_roots(const struct p101_env *env, struct p101_error *err, struct p101_tool_argv *command, const char *path);
static const char *choose_fact_tool(const struct p101_env *env, const struct arguments *args);

static void append_include_roots(const struct p101_env *env, struct p101_error *err, struct p101_tool_argv *command, const char *path)
{
    char include_arg[CONTRACT_PATH_LEN + sizeof("--cflag=-I/../include")];

    P101_TRACE_SCOPE(env);
    p101_snprintf(env, err, include_arg, sizeof(include_arg), "--cflag=-I%s", path);
    if(p101_error_has_no_error(err))
    {
        (void)p101_tool_argv_append(env, err, command, include_arg);
    }
    p101_snprintf(env, err, include_arg, sizeof(include_arg), "--cflag=-I%s/include", path);
    if(p101_error_has_no_error(err))
    {
        (void)p101_tool_argv_append(env, err, command, include_arg);
    }
    p101_snprintf(env, err, include_arg, sizeof(include_arg), "--cflag=-I%s/../include", path);
    if(p101_error_has_no_error(err))
    {
        (void)p101_tool_argv_append(env, err, command, include_arg);
    }
}

static const char *choose_fact_tool(const struct p101_env *env, const struct arguments *args)
{
    const char *tool;

    P101_TRACE_SCOPE(env);
    if(args->fact_tool_path != NULL && args->fact_tool_path[0] != '\0')
    {
        return args->fact_tool_path;
    }
    tool = p101_getenv(env, "P101_ERROR_CONTRACT_FACT_TOOL");
    if(tool != NULL && tool[0] != '\0')
    {
        return tool;
    }
    tool = p101_getenv(env, "P101_WRAPPER_AUDIT");
    if(tool != NULL && tool[0] != '\0')
    {
        return tool;
    }
    return "p101-wrapper-audit";
}

void p101_error_contract_build_fact_argv(const struct p101_env *env, struct p101_error *err, struct p101_tool_argv *command, const struct arguments *args)
{
    char        discovered_compile_db[CONTRACT_PATH_LEN];
    const char *compile_db;

    P101_TRACE_SCOPE(env);
    p101_tool_argv_init(command);
    if(p101_error_has_error(err))
    {
        return;
    }
    (void)p101_tool_argv_append(env, err, command, choose_fact_tool(env, args));
    (void)p101_tool_argv_append(env, err, command, "--emit-module-facts");
    compile_db = args->compile_db_path;
    if(compile_db == NULL && p101_c_facts_find_clang_compile_database(env, err, ".", discovered_compile_db, sizeof(discovered_compile_db)))
    {
        compile_db = discovered_compile_db;
    }
    if(compile_db != NULL && p101_error_has_no_error(err))
    {
        (void)p101_tool_argv_append(env, err, command, "--compile-db");
        (void)p101_tool_argv_append(env, err, command, compile_db);
        (void)p101_tool_argv_append(env, err, command, "--compile-db-only");
    }
    append_include_roots(env, err, command, ".");
    append_include_roots(env, err, command, "include");
    if(args->path_count == 0U)
    {
        append_include_roots(env, err, command, DEFAULT_SOURCE_PATH);
        (void)p101_tool_argv_append(env, err, command, DEFAULT_SOURCE_PATH);
    }
    else
    {
        for(size_t index = 0U; index < args->path_count && p101_error_has_no_error(err); index++)
        {
            append_include_roots(env, err, command, args->paths[index]);
        }
        for(size_t index = 0U; index < args->path_count && p101_error_has_no_error(err); index++)
        {
            (void)p101_tool_argv_append(env, err, command, args->paths[index]);
        }
    }
}
