#include "logging.h"

#include <fstream>
#include <iostream>

#ifdef __WINE__
#include "../wine-host/boost-fix.h"
#endif
#include <boost/process/environment.hpp>

/**
 * The environment variable indicating whether to log to a file. Will log to
 * STDERR if not specified.
 */
constexpr char logging_file_environment_variable[] = "YABRIDGE_DEBUG_FILE";

/**
 * The verbosity of the logging, defaults to `Logger::Verbosity::events` if
 * `logging_file_environment_variable` has been set and
 * `Logger::Verbosity::basic` otherwise.
 *
 * @see Logger::Verbosity
 */
constexpr char logging_verbosity_environment_variable[] =
    "YABRIDGE_DEBUG_VERBOSITY";

Logger::Logger(std::ostream&& stream,
               Verbosity verbosity_level,
               std::string prefix)
    : stream(stream), verbosity(verbosity_level), prefix(prefix) {}

Logger Logger::create_from_environment(std::string prefix) {
    auto env = boost::this_process::environment();
    std::string file_path = env.get(logging_file_environment_variable);
    std::string verbosity = env.get(logging_verbosity_environment_variable);

    // Default to `Verbosity::basic` if the environment variable has not
    // been set or if it is not an integer.
    Verbosity verbosity_level;
    try {
        verbosity_level = static_cast<Verbosity>(std::stoi(verbosity));
    } catch (const std::invalid_argument&) {
        verbosity_level = Verbosity::basic;
    }

    // If `file` points to a valid location then use create/truncate the
    // file and write all of the logs there, otherwise use STDERR
    std::ofstream log_file(file_path, std::fstream::out);
    if (log_file.is_open()) {
        return Logger(std::move(log_file), verbosity_level, prefix);
    } else {
        return Logger(std::move(std::cerr), verbosity_level, prefix);
    }
}
