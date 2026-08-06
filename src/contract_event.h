#ifndef P101_ERROR_CONTRACT_EVENT_H
#define P101_ERROR_CONTRACT_EVENT_H

#include "contract_types.h"
#include <stdbool.h>

bool p101_contract_event_needs_env(const struct contract_event *event);
bool p101_contract_event_needs_error(const struct contract_event *event);
bool p101_contract_event_is_not_after(const struct contract_event *candidate, const struct contract_event *event);

#endif
