// Vita3K emulator project
// Copyright (C) 2023 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include <config/functions.h>
#include <config/state.h>
#include <config/version.h>

#include <util/fs.h>
#include <util/log.h>
#include <util/string_utils.h>

#include <CLI11.hpp>
#include <vector>

#include <algorithm>
#include <exception>
#include <iostream>

namespace config {

static std::set<std::string> get_file_set(const fs::path &loc, bool dirs_only = true) {
    std::set<std::string> cur_set{};
    if (!fs::exists(loc)) {
        return cur_set;
    }

    fs::directory_iterator it{ loc };
    while (it != fs::directory_iterator{}) {
        if (dirs_only) {
            if (!it->path().empty() && fs::is_directory(it->path())) {
                cur_set.insert(it->path().stem().string());
            }
        } else {
            cur_set.insert(it->path().stem().string());
        }

        boost::system::error_code err{};
        it.increment(err);
    }
    return cur_set;
}

static fs::path check_path(const fs::path &output_path) {
    if (output_path.filename() != "config.yml") {
        if (!output_path.has_extension()) // assume it is a folder
            return output_path / "config.yml";
        if (output_path.extension() != ".yml")
            return "";
    }
    return output_path;
}

static ExitCode parse(Config &cfg, const fs::path &load_path, const fs::path &root_pref_path) {
    const auto loaded_path = check_path(load_path);
    if (loaded_path.empty() || !fs::exists(loaded_path)) {
        LOG_ERROR("Config file input path invalid (did you make sure to name the extension \".yml\"?)");
        return FileNotFound;
    }

    try {
        cfg.load_new_config(loaded_path);
    } catch (YAML::Exception &exception) {
        LOG_ERROR("Config file can't be loaded: Error: {}", exception.what());
        return FileNotFound;
    }

    if (cfg.pref_path.empty())
        cfg.pref_path = root_pref_path.string();
    else {
        if (string_utils::utf_to_wide(cfg.pref_path) != root_pref_path && !fs::exists(string_utils::utf_to_wide(cfg.pref_path))) {
            LOG_ERROR("Cannot find preference path: {}", cfg.pref_path);
            return InvalidApplicationPath;
        }
    }

    return Success;
}

ExitCode serialize_config(Config &cfg, const fs::path &output_path) {
    const auto output = check_path(output_path);
    if (output.empty())
        return InvalidApplicationPath;
    if (!fs::exists(output.parent_path()))
        fs::create_directories(output.parent_path());

    cfg.update_yaml();

    YAML::Emitter emitter;
    emitter << YAML::BeginDoc;
    emitter << cfg.yaml_node;
    emitter << YAML::EndDoc;

    fs::ofstream fo(output);
    if (!fo) {
        return InvalidApplicationPath;
    }

    fo << emitter.c_str();
    fo.close();

    return Success;
}

ExitCode init_config(Config &cfg, int argc, char **argv, const Root &root_paths) {
    // Always generate the default configuration file
    Config command_line{};
    serialize_config(command_line, root_paths.get_base_path() / "data/config/default.yml");
    // Load base path configuration by default; otherwise, move the default to the base path
    if (fs::exists(check_path(root_paths.get_base_path())))
        parse(cfg, root_paths.get_base_path(), root_paths.get_base_path());
    else
        fs::copy(root_paths.get_base_path() / "data/config/default.yml", root_paths.get_base_path() / "config.yml");

    // Declare all options
    CLI::App app{ "Vita3K Command Line Interface" }; // "--help,-h" is automatically generated
    app.allow_windows_style_options();
    app.allow_extras();
    app.enabled_by_default();
    app.get_formatter()->column_width(38);

    // clang-format off
    const auto ver = app.add_flag("--version,-v", "Print the version string")
        ->group("Options");

    // Positional options
    app.add_option("content-path", command_line.content_path, "Path of app in .vpk/.zip or folder of content to install & run")
        ->default_str({});

    // Grouped options
    auto input = app.add_option_group("Input", "Special options for Vita3K");
    input->add_option("--console,-z", command_line.console, "Starts the emulator in fullscreen mode.")
       ->default_val(false)->group("Input");
    input->add_option("--app-args,-Z", command_line.app_args, "Argument for app, use ', ' to separate arguments.")
        ->default_str("")->group("Input");
    input->add_option("--load-app-list,-a", command_line.load_app_list, "Starts the emulator with load app list.")
       ->default_val(false)->group("Input");
    input->add_option("--self,-S", command_line.self_path, "Path to the self to run inside Title ID")
        ->default_str("eboot.bin")->group("Input");
    input->add_option("--installed-path,-r", command_line.run_app_path, "Path of installed app to run")
        ->default_str({})->check(CLI::IsMember(get_file_set(fs::path(cfg.pref_path) / "ux0/app")))->group("Input");
    input->add_option("--recompile-shader,-s", command_line.recompile_shader_path, "Recompile the given PS Vita shader (GXP format) to SPIR_V / GLSL and quit")
        ->default_str({})->group("Input");
    input->add_option("--shader-cache,-D", command_line.shader_cache, "Enable shader cache to pre-compile it at boot up")
       ->default_val(true)->group("Input");
    input->add_option("--deleted-id,-d", command_line.delete_title_id, "Title ID of installed app to delete")
        ->default_str({})->check(CLI::IsMember(get_file_set(fs::path(cfg.pref_path) / "ux0/app")))->group("Input");
    auto input_pkg = input->add_option("--pkg", command_line.pkg_path, "Path of app (in .pkg format) to install")
        ->default_str({})->group("Input");
    auto input_zrif = input->add_option("--zrif", command_line.pkg_zrif, "zrif for the app (in .pkg format)")
        ->default_str({})->group("Input");
    input_pkg->needs(input_zrif);
    input_zrif->needs(input_pkg);

    auto config = app.add_option_group("Configuration", "Modify Vita3K's config.yml file");
    config->add_flag("--" + cfg[e_archive_log] + ",-A", command_line.archive_log, "Makes a duplicate of the log file with TITLE_ID and Game ID as title")
        ->group("Logging");
    config->add_option("--" + cfg[e_backend_renderer] + ",-B", command_line.backend_renderer, "Renderer backend to use")
        ->ignore_case()->check(CLI::IsMember(std::set<std::string>{ "OpenGL", "Vulkan" }))->group("Vita Emulation");
    config->add_flag("--" + cfg[e_color_surface_debug] + ",-C", command_line.color_surface_debug, "Save color surfaces")
        ->group("Vita Emulation");
    config->add_option("--config-location,-c", command_line.config_path, "Get a configuration file from a given location. If a filename is given, it must end with \".yml\", otherwise it will be assumed to be a directory. \nDefault loaded: <Vita3K>/config.yml \nDefaults: <Vita3K>/data/config/default.yml")
        ->group("YML");
    config->add_flag("!--keep-config,!-w", command_line.overwrite_config, "Do not modify the configuration file after loading.")
        ->group("YML");
    config->add_flag("--load-config,-f", command_line.load_config, "Load a configuration file. Setting --keep-config with this option preserves the configuration file.")
        ->group("YML");
    config->add_flag("--fullscreen,-F", command_line.fullscreen, "Starts the emulator in fullscreen mode.")
        ->group("YML");

    std::vector<std::string> lle_modules{};
    config->add_option("--" + cfg[e_lle_modules] + ",-m", lle_modules, "Load given (decrypted) OS modules from disk.\nSeparate by commas to specify multiple modules. Full path and extension should not be included, the following are assumed: vs0:sys/external/<name>.suprx\nExample: --lle-modules libscemp4,libngs")
        ->group("Modules");
    config->add_option("--" + cfg[e_log_level] + ",-l", command_line.log_level, "Logging level:\nTRACE = 0\nDEBUG = 1\nINFO = 2\nWARN = 3\nERROR = 4\nCRITICAL = 5\nOFF = 6")
        ->check(CLI::Range( 0, 6 ))->group("Logging");
    config->add_flag("--" + cfg[e_log_active_shaders] + ",-S", command_line.log_active_shaders, "Log Active Shaders")
        ->group("Logging");
    config->add_flag("--" + cfg[e_log_uniforms] + ",-U", command_line.log_uniforms, "Log Uniforms")
        ->group("Logging");
    // clang-format on

    // Parse the inputs
    try {
        app.parse(argc, argv);
    } catch (CLI::ParseError &e) {
        if (e.get_exit_code() != 0) {
            std::cout << "CLI parsing error: " << e.what() << "\n";
            return InitConfigFailed;
        }
    }

    if (!app.get_help_ptr()->empty()) {
        std::cout << app.help() << std::endl;
        return QuitRequested;
    }
    if (!ver->empty()) {
        std::cout << window_title << std::endl;
        return QuitRequested;
    }

    if (command_line.recompile_shader_path.has_value()) {
        cfg.recompile_shader_path = std::move(command_line.recompile_shader_path);
        return QuitRequested;
    }
    if (command_line.delete_title_id.has_value()) {
        cfg.delete_title_id = std::move(command_line.delete_title_id);
        return QuitRequested;
    }
    if (command_line.pkg_path.has_value() && command_line.pkg_zrif.has_value()) {
        cfg.pkg_path = std::move(command_line.pkg_path);
        cfg.pkg_zrif = std::move(command_line.pkg_zrif);
        return QuitRequested;
    }
    if (command_line.load_config || command_line.config_path != root_paths.get_base_path()) {
        if (command_line.config_path.empty()) {
            command_line.config_path = root_paths.get_base_path();
        } else {
            if (parse(command_line, command_line.config_path, root_paths.get_base_path()) != Success)
                return InitConfigFailed;
        }
    }

    if (cfg.console && (cfg.run_app_path || !cfg.content_path)) {
        LOG_ERROR("Console mode only supports vpk for now");
        return InitConfigFailed;
    }

    // Get LLE modules from the command line, otherwise get the modules from the YML file
    if (!lle_modules.empty()) {
        if (command_line.load_config) {
            command_line.lle_modules = vector_utils::merge_vectors(command_line.lle_modules, lle_modules);
        } else {
            command_line.lle_modules = std::move(lle_modules);
        }
    }

    // Merge configurations
    command_line.update_yaml();
    cfg += command_line;
    if (cfg.pref_path.empty())
        cfg.pref_path = root_paths.get_pref_path_string();

    if (!cfg.console) {
        LOG_INFO_IF(cfg.load_config, "Custom configuration file loaded successfully.");

        logging::set_level(static_cast<spdlog::level::level_enum>(cfg.log_level));

        LOG_INFO_IF(cfg.content_path, "input-content-path: {}", cfg.content_path->string());
        LOG_INFO_IF(cfg.run_app_path, "input-installed-path: {}", *cfg.run_app_path);
        LOG_INFO("{}: {}", cfg[e_backend_renderer], cfg.backend_renderer);
        LOG_INFO("{}: {}", cfg[e_log_level], cfg.log_level);
        LOG_INFO_IF(cfg.log_active_shaders, "{}: enabled", cfg[e_log_active_shaders]);
        LOG_INFO_IF(cfg.log_uniforms, "{}: enabled", cfg[e_log_uniforms]);
    }
    // Save any changes made in command-line arguments
    if (cfg.overwrite_config || !fs::exists(check_path(cfg.config_path))) {
        if (serialize_config(cfg, cfg.config_path) != Success)
            return InitConfigFailed;
    }

    return Success;
}

#ifdef TRACY_ENABLE
bool is_tracy_advanced_profiling_active_for_module(std::vector<std::string> &active_modules, std::string module, int *index) {
    // If we dont care about the index that means that its getting executed from an export
    // And because of that we know its sorted so we can use binary search so it
    // doesn't hurt performance as a standard std::find, also we just care for the result
    if (!index)
        return std::binary_search(active_modules.begin(), active_modules.end(), module);

    bool result = false;

    // Retrieve index for module name in the list of enabled modules
    auto iterator = std::find(active_modules.begin(), active_modules.end(), module);

    // Check the index the iterator references to (`std::find()` returns `last` if no match was found)
    if (iterator == active_modules.end()) {
        // Return false if no match is found in the active modules vector
        result = false;
    } else {
        // Return true if there was at least one match
        result = true;
    }

    // If index is being requested and the module name was found
    if (result) {
        // Calculate the distance between the first element in the module and
        *index = std::distance(active_modules.begin(), iterator);
    }

    return result;
}
#endif // TRACY_ENABLE

} // namespace config
