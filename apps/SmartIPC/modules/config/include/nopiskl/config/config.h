#pragma once
#include "nopiskl/common/types.h"

namespace nopiskl {
Json load_config(const std::string &defaults, const std::string &data_dir);
void validate_config(const Json &config);
void validate_patch(const std::string &section, const Json &patch);
} // namespace nopiskl
