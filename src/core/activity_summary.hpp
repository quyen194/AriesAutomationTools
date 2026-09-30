#pragma once
#include "workflow.hpp"
#include <string>

// One-line human readable description of an activity, including its variable
// bindings, e.g. "mouse_click left abs (10,20) +50ms  {x=$px}".
// Used by the activity list and by the log.
std::string ActivitySummary(const Activity& a);
