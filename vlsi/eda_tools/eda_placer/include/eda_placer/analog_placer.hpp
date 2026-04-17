#pragma once

#include "eda_placer/analog_problem.hpp"

#include <nlohmann/json.hpp>

namespace eda_placer::analog {

// Tool-neutral JSON schema:
//
// {
//   "outline": {"w": 200.0, "h": 120.0},
//   "instances": [
//     {
//       "id":"M1",
//       "device_type":"nmos",
//       "fixed": false,
//       "variants":[
//         {"name":"f1","w":2.0,"h":4.0,"pins":[{"name":"D","x":1.0,"y":4.0}]},
//         ...
//       ]
//     }
//   ],
//   "nets":[
//     {"name":"VINP","weight":2.0,"pins":[{"inst":"M1","pin":"G"},{"inst":"M2","pin":"G"}]}
//   ],
//   "symmetry":{
//     "vertical": true,
//     "axis": 100.0,          // optional; if omitted outline.w/2 is used
//     "pairs":[{"a":"M1","b":"M2"}]
//   },
//   "options": { ... optional overrides ... }
// }
//
// Returns:
// {
//   "placed":[{"id":"M1","x":..,"y":..,"w":..,"h":..,"variant":"f1","orient":"R0"}, ...],
//   "metrics": {...}
// }

[[nodiscard]] Problem problem_from_json(const nlohmann::json& j);
[[nodiscard]] Options options_from_json(const nlohmann::json& j);

[[nodiscard]] Result place(const Problem& p, const Options& opt);

[[nodiscard]] nlohmann::json result_to_json(const Result& r);

} // namespace eda_placer::analog

