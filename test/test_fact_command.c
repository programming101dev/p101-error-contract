#include "unity.h"
#include "arguments.h"
#include "constants.h"
#include "fact_command.h"
#include <p101_c/p101_string.h>
#include <p101_env/env.h>
#include <p101_error/error.h>

static struct p101_error *error;
static struct p101_env   *env;

void setUp(void)
{
    error = p101_error_create(false);
    env   = p101_env_create(error, NULL);
}

void tearDown(void)
{
    p101_env_destroy(env);
    p101_error_destroy(error);
}

static void test_cflags_precede_all_positional_source_paths(void)
{
    struct arguments args;
    char             command[MAX_COMMAND];
    const char      *first_source;
    const char      *cflag;

    p101_memset(env, &args, 0, sizeof(args));
    args.fact_tool_path = "p101-wrapper-audit";
    args.paths[0]       = "src";
    args.paths[1]       = "include";
    args.path_count     = 2U;

    p101_error_contract_build_fact_command(env, error, command, sizeof(command), &args);

    TEST_ASSERT_FALSE(p101_error_has_error(error));
    cflag        = p101_strstr(env, command, " --cflag=");
    first_source = p101_strstr(env, command, " 'src'");
    TEST_ASSERT_NOT_NULL(cflag);
    TEST_ASSERT_NOT_NULL(first_source);
    TEST_ASSERT_TRUE(cflag < first_source);
    TEST_ASSERT_NULL(p101_strstr(env, first_source, "--cflag="));
    TEST_ASSERT_NOT_NULL(p101_strstr(env, first_source, " 'include'"));
}

static void test_explicit_compile_database_precedes_source_paths(void)
{
    struct arguments args;
    char             command[MAX_COMMAND];
    const char      *compile_db;
    const char      *first_source;

    p101_memset(env, &args, 0, sizeof(args));
    args.fact_tool_path  = "p101-wrapper-audit";
    args.compile_db_path = "/tmp/project with spaces/compile_commands.json";
    args.paths[0]        = "src";
    args.path_count      = 1U;

    p101_error_contract_build_fact_command(env, error, command, sizeof(command), &args);

    TEST_ASSERT_FALSE(p101_error_has_error(error));
    compile_db  = p101_strstr(env, command, " --compile-db='/tmp/project with spaces/compile_commands.json'");
    first_source = p101_strstr(env, command, " 'src'");
    TEST_ASSERT_NOT_NULL(compile_db);
    TEST_ASSERT_NOT_NULL(first_source);
    TEST_ASSERT_TRUE(compile_db < first_source);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_cflags_precede_all_positional_source_paths);
    RUN_TEST(test_explicit_compile_database_precedes_source_paths);
    return UNITY_END();
}
