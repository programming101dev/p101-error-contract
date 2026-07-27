#include "report.h"
#include "constants.h"
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_string.h>

static void json_string(const struct p101_env *env, struct p101_error *err, const char *text);

void p101_error_contract_report_begin(const struct p101_env *env, struct p101_error *err, struct contract_report *report, const struct arguments *args)
{
    P101_TRACE(env);
    p101_memset(env, report, 0, sizeof(*report));
    report->json               = args->json;
    report->quiet              = args->quiet;
    report->first_json_finding = true;

    if(report->json)
    {
        p101_fputs(env, err, "{\"schema\":\"p101-error-contract-v1\",\"findings\":[", stdout);
    }
}

void p101_error_contract_report_finding(const struct p101_env *env, struct p101_error *err, struct contract_report *report, const char *code, const char *path, size_t line, const char *function_name, const char *message)
{
    P101_TRACE(env);
    report->findings++;

    if(report->json)
    {
        if(!report->first_json_finding)
        {
            p101_fputs(env, err, ",", stdout);
        }
        report->first_json_finding = false;

        p101_fputs(env, err, "{\"code\":", stdout);
        json_string(env, err, code);
        p101_fputs(env, err, ",\"path\":", stdout);
        json_string(env, err, path);
        p101_fprintf(env, err, stdout, ",\"line\":%zu,\"function\":", line);
        json_string(env, err, function_name);
        p101_fputs(env, err, ",\"message\":", stdout);
        json_string(env, err, message);
        p101_fputs(env, err, "}", stdout);
    }
    else
    {
        p101_fprintf(env, err, stdout, "%s:%zu: %s: %s", path, line, code, message);
        if(function_name != NULL && function_name[0] != '\0')
        {
            p101_fprintf(env, err, stdout, " [%s]", function_name);
        }
        p101_fputs(env, err, "\n", stdout);
    }
}

void p101_error_contract_report_end(const struct p101_env *env, struct p101_error *err, struct contract_report *report)
{
    P101_TRACE(env);

    if(report->json)
    {
        p101_fprintf(env, err, stdout, "],\"summary\":{\"files_scanned\":%zu,\"findings\":%zu}}\n", report->files_scanned, report->findings);
    }
    else if(!report->quiet)
    {
        p101_fprintf(env, err, stdout, "p101-error-contract: scanned %zu file(s), found %zu issue(s)\n", report->files_scanned, report->findings);
    }
}

static void json_string(const struct p101_env *env, struct p101_error *err, const char *text)
{
    P101_TRACE(env);
    p101_fputs(env, err, "\"", stdout);

    if(text != NULL)
    {
        const unsigned char *cursor;

        cursor = (const unsigned char *)text;
        while(*cursor != '\0')
        {
            switch(*cursor)
            {
                case '\"':
                    p101_fputs(env, err, "\\\"", stdout);
                    break;
                case '\\':
                    p101_fputs(env, err, "\\\\", stdout);
                    break;
                case '\n':
                    p101_fputs(env, err, "\\n", stdout);
                    break;
                case '\r':
                    p101_fputs(env, err, "\\r", stdout);
                    break;
                case '\t':
                    p101_fputs(env, err, "\\t", stdout);
                    break;
                default:
                    if(*cursor < JSON_CONTROL_CHAR_LIMIT)
                    {
                        p101_fprintf(env, err, stdout, "\\u%04x", (unsigned)*cursor);
                    }
                    else
                    {
                        p101_fprintf(env, err, stdout, "%c", *cursor);
                    }
                    break;
            }
            cursor++;
        }
    }

    p101_fputs(env, err, "\"", stdout);
}
