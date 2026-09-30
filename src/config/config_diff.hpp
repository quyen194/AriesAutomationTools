#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// Human readable differences between two config documents (as written by
// ConfigManager::ToJson), one line per change. Workflows and activities are
// matched by id, so inserting / moving an item reports just that item, e.g.
//   workflow "Farm" > activities: added #3 mouse_click [a1b2c3d4]
//   workflow "Farm" > activities #2 wait [9f8e7d6c] > duration_ms: 500 -> 800
std::vector<std::string> DiffConfigJson(const nlohmann::ordered_json& before,
                                        const nlohmann::ordered_json& after);
