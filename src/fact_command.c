#include "fact_command.h"
#include "constants.h"
#include "errors.h"
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_stdlib.h>
#include <p101_c/p101_string.h>
#include <p101_c_facts/project.h>
#include <p101_posix/p101_unistd.h>
#include <stdbool.h>
#include <unistd.h>

static void        append_checked(const struct p101_env *env, struct p101_error *err, char *command, size_t command_size, const char *text);
static void        append_char_checked(const struct p101_env *env, struct p101_error *err, char *command, size_t command_size, char ch);
static void        append_shell_quoted(const struct p101_env *env, struct p101_error *err, char *command, size_t command_size, const char *text);
static void        append_cflag(const struct p101_env *env, struct p101_error *err, char *command, size_t command_size, const char *flag);
static void        append_compile_database(const struct p101_env *env, struct p101_error *err, char *command, size_t command_size, const char *path);
static void        append_include_roots(const struct p101_env *env, struct p101_error *err, char *command, size_t command_size, const char *path);
static const char *choose_fact_tool(const struct p101_env *env, struct p101_error *err, const struct arguments *args);
static bool        executable_exists(const struct p101_env *env, struct p101_error *err, const char *path);

static void append_checked(const struct p101_env *env, struct p101_error *err, char *command, size_t command_size, const char *text)
{
    size_t used;
    size_t extra;

    P101_TRACE(env);
    used  = p101_strlen(env, command);
    extra = p101_strlen(env, text);

    if(used + extra >= command_size)
    {
        P101_ERROR_RAISE_USER(err, "The p101-wrapper-audit command is too long.", ERR_USAGE);
        goto done;
    }

    p101_strncpy(env, command + used, text, command_size - used - 1U);
    command[command_size - 1U] = '\0';

done:
    return;
}

static void append_char_checked(const struct p101_env *env, struct p101_error *err, char *command, size_t command_size, char ch)
{
    char text[2];

    P101_TRACE(env);
    text[0] = ch;
    text[1] = '\0';
    append_checked(env, err, command, command_size, text);
}

static void append_shell_quoted(const struct p101_env *env, struct p101_error *err, char *command, size_t command_size, const char *text)
{
    P101_TRACE(env);
    append_char_checked(env, err, command, command_size, '\'');
    for(size_t i = 0U; text[i] != '\0' && p101_error_has_no_error(err); i++)
    {
        if(text[i] == '\'')
        {
            append_checked(env, err, command, command_size, "'\\''");
        }
        else
        {
            append_char_checked(env, err, command, command_size, text[i]);
        }
    }
    append_char_checked(env, err, command, command_size, '\'');
}

static void append_cflag(const struct p101_env *env, struct p101_error *err, char *command, size_t command_size, const char *flag)
{
    P101_TRACE(env);
    append_checked(env, err, command, command_size, " --cflag=");
    append_shell_quoted(env, err, command, command_size, flag);
}

static void append_compile_database(const struct p101_env *env, struct p101_error *err, char *command, size_t command_size, const char *path)
{
    P101_TRACE(env);
    append_checked(env, err, command, command_size, " --compile-db=");
    append_shell_quoted(env, err, command, command_size, path);
}

static void append_include_roots(const struct p101_env *env, struct p101_error *err, char *command, size_t command_size, const char *path)
{
    char include_arg[CONTRACT_PATH_LEN];

    P101_TRACE(env);
    p101_snprintf(env, err, include_arg, sizeof(include_arg), "-I%s", path);
    append_cflag(env, err, command, command_size, include_arg);
    p101_snprintf(env, err, include_arg, sizeof(include_arg), "-I%s/include", path);
    append_cflag(env, err, command, command_size, include_arg);
    p101_snprintf(env, err, include_arg, sizeof(include_arg), "-I%s/../include", path);
    append_cflag(env, err, command, command_size, include_arg);
}

static const char *choose_fact_tool(const struct p101_env *env, struct p101_error *err, const struct arguments *args)
{
    const char *tool;

    P101_TRACE(env);
    tool = args->fact_tool_path;
    if(tool != NULL && tool[0] != '\0')
    {
        goto done;
    }

    tool = p101_getenv(env, "P101_ERROR_CONTRACT_FACT_TOOL");
    if(tool != NULL && tool[0] != '\0')
    {
        goto done;
    }

    tool = p101_getenv(env, "P101_WRAPPER_AUDIT");
    if(tool != NULL && tool[0] != '\0')
    {
        goto done;
    }

    tool = "../p101-wrapper-audit/p101-wrapper-audit";
    if(executable_exists(env, err, tool))
    {
        goto done;
    }

    tool = "../../programs/p101-wrapper-audit/p101-wrapper-audit";
    if(executable_exists(env, err, tool))
    {
        goto done;
    }

    tool = "p101-wrapper-audit";

done:
    return tool;
}

static bool executable_exists(const struct p101_env *env, struct p101_error *err, const char *path)
{
    bool ret_val;

    P101_TRACE(env);
    ret_val = false;
    if(p101_access(env, err, path, X_OK) == 0)
    {
        ret_val = true;
    }
    if(p101_error_has_error(err))
    {
        p101_error_reset(err);
    }
    return ret_val;
}

void p101_error_contract_build_fact_command(const struct p101_env *env, struct p101_error *err, char *command, size_t command_size, const struct arguments *args)
{
    char        discovered_compile_db[CONTRACT_PATH_LEN];
    const char *compile_db;
    const char *tool;

    P101_TRACE(env);
    command[0] = '\0';
    tool       = choose_fact_tool(env, err, args);
    append_shell_quoted(env, err, command, command_size, tool);
    append_checked(env, err, command, command_size, " --emit-module-facts");
    compile_db = args->compile_db_path;
    if(compile_db == NULL && p101_c_facts_find_clang_compile_database(env, err, ".", discovered_compile_db, sizeof(discovered_compile_db)))
    {
        compile_db = discovered_compile_db;
    }
    if(compile_db != NULL && p101_error_has_no_error(err))
    {
        append_compile_database(env, err, command, command_size, compile_db);
    }
    append_include_roots(env, err, command, command_size, ".");
    append_include_roots(env, err, command, command_size, "include");

    if(args->path_count == 0U)
    {
        append_include_roots(env, err, command, command_size, DEFAULT_SOURCE_PATH);
    }
    else
    {
        for(size_t i = 0U; i < args->path_count && p101_error_has_no_error(err); i++)
        {
            append_include_roots(env, err, command, command_size, args->paths[i]);
        }
    }

    if(args->path_count == 0U)
    {
        append_char_checked(env, err, command, command_size, ' ');
        append_shell_quoted(env, err, command, command_size, DEFAULT_SOURCE_PATH);
    }
    else
    {
        for(size_t i = 0U; i < args->path_count && p101_error_has_no_error(err); i++)
        {
            append_char_checked(env, err, command, command_size, ' ');
            append_shell_quoted(env, err, command, command_size, args->paths[i]);
        }
    }

    append_checked(env, err, command, command_size, " 2>&1");
}
