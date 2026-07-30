struct p101_env;
struct p101_error;
int p101_open(const struct p101_env *, struct p101_error *, const char *, int, ...);
#define P101_TRACE_SCOPE(value) ((void)(value))
#define NULL ((void *)0)

static struct p101_env   *env;
static struct p101_error *err;

static void bad(void)
{
    P101_TRACE_SCOPE(env);
    p101_open(env, err, "x", 0);
    p101_open(env, NULL, "x", 0);
    p101_open(env, err, "x", 0);
}

static void good(const struct p101_env *function_env, struct p101_error *function_err)
{
    p101_open(function_env, function_err, "x", 0);
}
