#include "contract.h"
#include "constants.h"
#include "errors.h"
#include "report.h"
#include <dirent.h>
#include <errno.h>
#include <p101_c/p101_ctype.h>
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_string.h>
#include <p101_posix/p101_dirent.h>
#include <p101_posix/sys/p101_stat.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/stat.h>

struct function_state
{
    bool active;
    int  brace_depth;
    bool has_env;
    bool has_err;
    bool env_reported;
    bool err_reported;
    char name[FUNCTION_NAME_LEN];
};

static int  scan_path(const struct p101_env *env, struct p101_error *err, const char *path, struct contract_report *report);
static int  scan_directory(const struct p101_env *env, struct p101_error *err, const char *path, struct contract_report *report);
static int  scan_file(const struct p101_env *env, struct p101_error *err, const char *path, struct contract_report *report);
static void process_line(const struct p101_env *env, struct p101_error *err, struct contract_report *report, struct function_state *state, const char *path, const char *line, size_t line_number, char *pending_header, size_t pending_header_size);
static void process_active_line(const struct p101_env *env, struct p101_error *err, struct contract_report *report, struct function_state *state, const char *path, const char *line, size_t line_number);
static void start_function_if_present(const struct p101_env *env, struct function_state *state, const char *header, const char *line);
static void append_header_line(const struct p101_env *env, struct p101_error *err, char *pending_header, size_t pending_header_size, const char *line);
static void reset_function_state(const struct p101_env *env, struct function_state *state);
static bool is_source_file(const struct p101_env *env, const char *path);
static bool should_skip_dir(const struct p101_env *env, const char *name);
static bool is_dot_entry(const struct p101_env *env, const char *name);
static bool line_is_preprocessor(const struct p101_env *env, const char *line);
static bool line_starts_header(const struct p101_env *env, const char *line);
static bool line_ends_declaration(const struct p101_env *env, const char *line);
static bool contains_identifier(const struct p101_env *env, const char *text, const char *needle);
static bool contains_any_identifier(const struct p101_env *env, const char *text, const char *const needles[]);
static bool contains_p101_call_needing_env(const struct p101_env *env, const char *line);
static bool contains_p101_error_contract_use(const struct p101_env *env, const char *line);
static bool parse_function_name(const struct p101_env *env, const char *header, char *name, size_t name_size);
static bool is_keyword_function_name(const struct p101_env *env, const char *name);
static bool is_identifier_char(const struct p101_env *env, int ch);
static int  brace_delta(const char *line);

int p101_error_contract_run(const struct p101_env *env, struct p101_error *err, const struct arguments *args)
{
    struct contract_report report;
    int                    ret_val;
    const char            *default_path;

    P101_TRACE(env);
    ret_val      = EXIT_SUCCESS;
    default_path = DEFAULT_SOURCE_PATH;
    p101_error_contract_report_begin(env, err, &report, args);

    if(args->path_count == 0)
    {
        ret_val = scan_path(env, err, default_path, &report);
    }
    else
    {
        int i;

        for(i = 0; i < args->path_count && p101_error_has_no_error(err); i++)
        {
            if(scan_path(env, err, args->paths[i], &report) == EXIT_TROUBLE)
            {
                ret_val = EXIT_TROUBLE;
                break;
            }
        }
    }

    if(p101_error_has_no_error(err))
    {
        p101_error_contract_report_end(env, err, &report);
    }

    if(p101_error_has_error(err) || ret_val == EXIT_TROUBLE)
    {
        return EXIT_TROUBLE;
    }

    return (report.findings == 0U) ? EXIT_SUCCESS : EXIT_FINDINGS;
}

static int scan_path(const struct p101_env *env, struct p101_error *err, const char *path, struct contract_report *report)    // NOLINT(misc-no-recursion)
{
    struct stat st;

    P101_TRACE(env);
    if(p101_lstat(env, err, path, &st) != 0)
    {
        return EXIT_TROUBLE;
    }

    if(S_ISDIR(st.st_mode))
    {
        return scan_directory(env, err, path, report);
    }

    if(S_ISREG(st.st_mode) && is_source_file(env, path))
    {
        return scan_file(env, err, path, report);
    }

    return EXIT_SUCCESS;
}

static int scan_directory(const struct p101_env *env, struct p101_error *err, const char *path, struct contract_report *report)    // NOLINT(misc-no-recursion)
{
    DIR *dir;
    int  ret_val;

    P101_TRACE(env);
    ret_val = EXIT_SUCCESS;
    dir     = p101_opendir(env, err, path);

    if(dir == NULL)
    {
        return EXIT_TROUBLE;
    }

    for(;;)
    {
        struct dirent *entry;
        char           child[CONTRACT_PATH_LEN];
        int            written;

        errno = 0;
        entry = readdir(dir);    // NOLINT(concurrency-mt-unsafe): shared directory stream is not used across threads.

        if(entry == NULL)
        {
            if(errno != 0)
            {
                P101_ERROR_RAISE_ERRNO(err, errno);
                ret_val = EXIT_TROUBLE;
            }
            break;
        }

        if(is_dot_entry(env, entry->d_name) || should_skip_dir(env, entry->d_name))
        {
            continue;
        }

        written = p101_snprintf(env, err, child, sizeof(child), "%s/%s", path, entry->d_name);
        if(p101_error_has_error(err))
        {
            ret_val = EXIT_TROUBLE;
            break;
        }
        if(written < 0 || written >= CONTRACT_PATH_LEN)
        {
            P101_ERROR_RAISE_USER(err, "Path is too long while scanning.", ERR_TOOL);
            ret_val = EXIT_TROUBLE;
            break;
        }

        if(scan_path(env, err, child, report) == EXIT_TROUBLE)
        {
            ret_val = EXIT_TROUBLE;
            break;
        }
    }

    if(p101_closedir(env, err, dir) != 0)
    {
        ret_val = EXIT_TROUBLE;
    }

    return ret_val;
}

static int scan_file(const struct p101_env *env, struct p101_error *err, const char *path, struct contract_report *report)
{
    struct function_state state;
    char                  line[READ_BUF_LEN];
    char                  pending_header[FUNCTION_HEADER_LEN];
    FILE                 *file;
    size_t                line_number;
    int                   ret_val;

    P101_TRACE(env);
    reset_function_state(env, &state);
    pending_header[0] = '\0';
    line_number       = 0U;
    ret_val           = EXIT_SUCCESS;
    file              = p101_fopen(env, err, path, "r");

    if(file == NULL)
    {
        return EXIT_TROUBLE;
    }

    report->files_scanned++;

    while(p101_fgets(env, err, line, sizeof(line), file) != NULL && p101_error_has_no_error(err))
    {
        line_number++;
        process_line(env, err, report, &state, path, line, line_number, pending_header, sizeof(pending_header));
    }

    if(p101_error_has_error(err) || p101_ferror(env, file))
    {
        ret_val = EXIT_TROUBLE;
    }

    if(p101_fclose(env, err, file) != 0)
    {
        ret_val = EXIT_TROUBLE;
    }

    return ret_val;
}

static void process_line(const struct p101_env *env, struct p101_error *err, struct contract_report *report, struct function_state *state, const char *path, const char *line, size_t line_number, char *pending_header, size_t pending_header_size)
{
    P101_TRACE(env);

    if(state->active)
    {
        process_active_line(env, err, report, state, path, line, line_number);
        state->brace_depth += brace_delta(line);
        if(state->brace_depth <= 0)
        {
            reset_function_state(env, state);
        }
        return;
    }

    if(line_is_preprocessor(env, line))
    {
        pending_header[0] = '\0';
        return;
    }

    if(pending_header[0] == '\0' && !line_starts_header(env, line))
    {
        return;
    }

    append_header_line(env, err, pending_header, pending_header_size, line);
    if(p101_error_has_error(err))
    {
        return;
    }

    if(p101_strchr(env, line, '{') != NULL)
    {
        start_function_if_present(env, state, pending_header, line);
        pending_header[0] = '\0';

        if(state->active)
        {
            process_active_line(env, err, report, state, path, line, line_number);
            if(state->brace_depth <= 0)
            {
                reset_function_state(env, state);
            }
        }
    }
    else if(line_ends_declaration(env, line))
    {
        pending_header[0] = '\0';
    }
}

static void process_active_line(const struct p101_env *env, struct p101_error *err, struct contract_report *report, struct function_state *state, const char *path, const char *line, size_t line_number)
{
    P101_TRACE(env);

    if(contains_identifier(env, line, "env") || contains_identifier(env, line, "p101_env"))
    {
        state->has_env = true;
    }

    if(contains_identifier(env, line, "err") || contains_identifier(env, line, "p101_error"))
    {
        state->has_err = true;
    }

    if(!state->has_env && !state->env_reported && contains_p101_call_needing_env(env, line))
    {
        p101_error_contract_report_finding(env, err, report, "P101-ERR-001", path, line_number, state->name, "p101 call or P101_TRACE appears before a visible p101_env/env contract");
        state->env_reported = true;
    }

    if(!state->has_err && !state->err_reported && contains_p101_error_contract_use(env, line))
    {
        p101_error_contract_report_finding(env, err, report, "P101-ERR-002", path, line_number, state->name, "fallible p101 call or error macro appears before a visible p101_error/err contract");
        state->err_reported = true;
    }
}

static void start_function_if_present(const struct p101_env *env, struct function_state *state, const char *header, const char *line)
{
    P101_TRACE(env);

    reset_function_state(env, state);
    if(!parse_function_name(env, header, state->name, sizeof(state->name)))
    {
        return;
    }

    state->active       = true;
    state->brace_depth  = brace_delta(line);
    state->has_env      = false;
    state->has_err      = false;
    state->env_reported = false;
    state->err_reported = false;

    if(contains_identifier(env, header, "env") || contains_identifier(env, header, "p101_env"))
    {
        state->has_env = true;
    }

    if(contains_identifier(env, header, "err") || contains_identifier(env, header, "p101_error"))
    {
        state->has_err = true;
    }
}

static void append_header_line(const struct p101_env *env, struct p101_error *err, char *pending_header, size_t pending_header_size, const char *line)
{
    size_t current_len;
    size_t line_len;

    P101_TRACE(env);
    current_len = p101_strlen(env, pending_header);
    line_len    = p101_strlen(env, line);

    if(current_len + line_len + 2U >= pending_header_size)
    {
        P101_ERROR_RAISE_USER(err, "Function header is too long for heuristic scanner.", ERR_TOOL);
        return;
    }

    if(current_len != 0U)
    {
        pending_header[current_len]      = ' ';
        pending_header[current_len + 1U] = '\0';
        current_len++;
    }

    p101_strncpy(env, &pending_header[current_len], line, pending_header_size - current_len - 1U);
    pending_header[pending_header_size - 1U] = '\0';
}

static void reset_function_state(const struct p101_env *env, struct function_state *state)
{
    P101_TRACE(env);
    p101_memset(env, state, 0, sizeof(*state));
}

static bool is_source_file(const struct p101_env *env, const char *path)
{
    const char *dot;

    P101_TRACE(env);
    dot = p101_strrchr(env, path, '.');

    if(dot == NULL)
    {
        return false;
    }

    if(p101_strcmp(env, dot, ".c") == 0 || p101_strcmp(env, dot, ".h") == 0 || p101_strcmp(env, dot, ".cc") == 0 || p101_strcmp(env, dot, ".cpp") == 0 || p101_strcmp(env, dot, ".hh") == 0 || p101_strcmp(env, dot, ".hpp") == 0)
    {
        return true;
    }

    return false;
}

static bool should_skip_dir(const struct p101_env *env, const char *name)
{
    P101_TRACE(env);
    if(p101_strcmp(env, name, ".git") == 0 || p101_strcmp(env, name, "build") == 0 || p101_strncmp(env, name, "build-", BUILD_PREFIX_LEN) == 0 || p101_strncmp(env, name, "cmake-build-", CMAKE_BUILD_PREFIX_LEN) == 0 ||
       p101_strncmp(env, name, "coverage-", COVERAGE_PREFIX_LEN) == 0 || p101_strcmp(env, name, "findings") == 0 || p101_strcmp(env, name, "artifacts") == 0)
    {
        return true;
    }

    return false;
}

static bool is_dot_entry(const struct p101_env *env, const char *name)
{
    P101_TRACE(env);
    if(p101_strcmp(env, name, ".") == 0 || p101_strcmp(env, name, "..") == 0)
    {
        return true;
    }

    return false;
}

static bool line_is_preprocessor(const struct p101_env *env, const char *line)
{
    const char *cursor;

    P101_TRACE(env);
    cursor = line;

    while(*cursor != '\0' && p101_isspace(env, (unsigned char)*cursor))
    {
        cursor++;
    }

    return *cursor == '#';
}

static bool line_starts_header(const struct p101_env *env, const char *line)
{
    P101_TRACE(env);
    if(p101_strchr(env, line, '(') != NULL && p101_strchr(env, line, ';') == NULL && p101_strchr(env, line, '=') == NULL)
    {
        return true;
    }

    return false;
}

static bool line_ends_declaration(const struct p101_env *env, const char *line)
{
    P101_TRACE(env);
    return p101_strchr(env, line, ';') != NULL;
}

static bool contains_identifier(const struct p101_env *env, const char *text, const char *needle)
{
    const char *cursor;
    size_t      needle_len;

    P101_TRACE(env);
    needle_len = p101_strlen(env, needle);
    cursor     = text;

    while((cursor = p101_strstr(env, cursor, needle)) != NULL)
    {
        char before;
        char after;

        before = (cursor == text) ? '\0' : cursor[-1];
        after  = cursor[needle_len];

        if(!is_identifier_char(env, (unsigned char)before) && !is_identifier_char(env, (unsigned char)after))
        {
            return true;
        }

        cursor++;
    }

    return false;
}

static bool contains_any_identifier(const struct p101_env *env, const char *text, const char *const needles[])
{
    size_t i;

    P101_TRACE(env);
    for(i = 0U; needles[i] != NULL; i++)
    {
        if(contains_identifier(env, text, needles[i]))
        {
            return true;
        }
    }

    return false;
}

static bool contains_p101_call_needing_env(const struct p101_env *env, const char *line)
{
    static const char *const env_calls[] = {
        "P101_TRACE",   "p101_access", "p101_calloc",   "p101_chdir", "p101_closedir", "p101_close",  "p101_dup",    "p101_dup2",    "p101_env_destroy", "p101_execv",   "p101_execve", "p101_execvp", "p101_fclose", "p101_fgets",   "p101_fopen",
        "p101_fprintf", "p101_fputs",  "p101_fread",    "p101_fstat", "p101_lstat",    "p101_malloc", "p101_memset", "p101_mkdir",   "p101_open",        "p101_opendir", "p101_pipe",   "p101_printf", "p101_read",   "p101_realloc", "p101_readdir",
        "p101_rename",  "p101_rmdir",  "p101_snprintf", "p101_stat",  "p101_strchr",   "p101_strcmp", "p101_strlen", "p101_strncmp", "p101_strrchr",     "p101_strstr",  "p101_unlink", "p101_write",  NULL,
    };

    P101_TRACE(env);
    return contains_any_identifier(env, line, env_calls);
}

static bool contains_p101_error_contract_use(const struct p101_env *env, const char *line)
{
    static const char *const fallible[] = {
        "P101_ERROR_RAISE_ERRNO",
        "P101_ERROR_RAISE_SYSTEM",
        "P101_ERROR_RAISE_USER",
        "p101_access",
        "p101_calloc",
        "p101_chdir",
        "p101_closedir",
        "p101_close",
        "p101_dup",
        "p101_dup2",
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
    return contains_any_identifier(env, line, fallible);
}

static bool parse_function_name(const struct p101_env *env, const char *header, char *name, size_t name_size)
{
    const char *paren;
    const char *end;
    const char *start;
    size_t      len;
    size_t      i;

    P101_TRACE(env);
    paren = p101_strchr(env, header, '(');
    if(paren == NULL)
    {
        return false;
    }

    end = paren;
    while(end > header && p101_isspace(env, (unsigned char)end[-1]))
    {
        end--;
    }

    start = end;
    while(start > header && is_identifier_char(env, (unsigned char)start[-1]))
    {
        start--;
    }

    len = (size_t)(end - start);
    if(len == 0U || len >= name_size)
    {
        return false;
    }

    for(i = 0U; i < len; i++)
    {
        name[i] = start[i];
    }
    name[len] = '\0';

    if(is_keyword_function_name(env, name))
    {
        return false;
    }

    return true;
}

static bool is_keyword_function_name(const struct p101_env *env, const char *name)
{
    static const char *const keywords[] = {
        "if",
        "for",
        "while",
        "switch",
        "return",
        "sizeof",
        "_Generic",
        NULL,
    };

    P101_TRACE(env);
    return contains_any_identifier(env, name, keywords);
}

static bool is_identifier_char(const struct p101_env *env, int ch)
{
    P101_TRACE(env);
    if(p101_isalnum(env, ch) != 0 || ch == '_')
    {
        return true;
    }

    return false;
}

static int brace_delta(const char *line)
{
    int         delta;
    const char *cursor;

    delta  = 0;
    cursor = line;

    while(*cursor != '\0')
    {
        if(*cursor == '{')
        {
            delta++;
        }
        else if(*cursor == '}')
        {
            delta--;
        }
        cursor++;
    }

    return delta;
}
