#include "contract_event.h"

bool p101_contract_event_needs_env(const struct contract_event *event)
{
    bool needs_env;

    needs_env = event->kind == CONTRACT_EVENT_TRACE_USE;
    if(!needs_env && event->kind == CONTRACT_EVENT_CALL && event->needs_env)
    {
        needs_env = true;
    }
    return needs_env;
}

bool p101_contract_event_needs_error(const struct contract_event *event)
{
    bool needs_error;

    needs_error = event->kind == CONTRACT_EVENT_ERROR_CHECK;
    if(!needs_error && event->kind == CONTRACT_EVENT_CALL && event->needs_error)
    {
        needs_error = true;
    }
    return needs_error;
}

bool p101_contract_event_is_not_after(const struct contract_event *candidate, const struct contract_event *event)
{
    bool ordered;

    if(candidate->start != 0U && event->start != 0U)
    {
        ordered = candidate->start <= event->start;
    }
    else
    {
        ordered = candidate->line <= event->line;
    }
    return ordered;
}
