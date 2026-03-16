#pragma once

#include "greenflame_core/app_config.h"

namespace greenflame {

[[nodiscard]] std::filesystem::path Get_app_config_dir();
[[nodiscard]] std::filesystem::path Get_config_file_path();
[[nodiscard]] core::AppConfig Load_app_config();
[[nodiscard]] bool Save_app_config(core::AppConfig const &config);

} // namespace greenflame
