// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2024 Robbert van der Helm
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

use anyhow::Result;
use clap::builder::TypedValueParser;
use clap::{command, value_parser, Arg, ArgAction, Command};
use colored::Colorize;
use std::collections::HashSet;
use std::env;
use std::path::{Path, PathBuf};

use crate::actions::Vst2Location;
use crate::config::Config;

mod actions;
mod config;
mod files;
mod symbols;
mod util;
mod vst3_moduleinfo;

fn main() -> Result<()> {
    // We'll modify our `PATH` environment variable so it matches up with
    // `get_augmented_search_path()` from `src/plugin/utils.h` for easier setup
    let yabridge_home = config::yabridge_directories()?.get_data_home();
    env::set_var(
        "PATH",
        match env::var("PATH") {
            Ok(path) => format!("{}:{}", path, yabridge_home.display()),
            _ => format!("{}", yabridge_home.display()),
        },
    );

    let mut config = Config::read()?;

    // Used for parsing and validation in `yabridgectl rm <path>`
    let plugin_directories: HashSet<PathBuf> = config.plugin_dirs.iter().cloned().collect();
    // Used for parsing and validation in `yabridgectl blacklist rm <path>`
    let blacklist_entries: HashSet<PathBuf> = config.blacklist.iter().cloned().collect();

    let matches = command!()
        .subcommand_required(true)
        .arg_required_else_help(true)
        .subcommand(
            Command::new("add")
                .about("Add a plugin install location")
                .display_order(1)
                .arg(
                    Arg::new("path")
                        .help("Path to a directory containing Windows VST2, VST3, or CLAP plugins")
                        .value_parser(parse_directory_path)
                        .required(true),
                ),
        )
        .subcommand(
            Command::new("rm")
                .about("Remove a plugin install location")
                .display_order(2)
                .arg(
                    Arg::new("path")
                        .help("Path to a previously added directory")
                        .value_parser(parse_path_from_set(plugin_directories))
                        .required(true),
                ),
        )
        .subcommand(
            Command::new("list")
                .about("List the plugin install locations")
                .display_order(3),
        )
        .subcommand(
            Command::new("status")
                .about("Show the installation status for all plugins")
                .display_order(4),
        )
        .subcommand(
            Command::new("sync")
                .about("Set up or update yabridge for all plugins")
                .display_order(100)
                .arg(
                    Arg::new("force")
                        .short('f')
                        .long("force")
                        .help("Always update files, even not necessary")
                        .action(ArgAction::SetTrue),
                )
                .arg(
                    Arg::new("no-verify")
                        .short('n')
                        .long("no-verify")
                        .help("Skip post-installation setup checks")
                        .action(ArgAction::SetTrue),
                )
                .arg(
                    Arg::new("prune")
                        .short('p')
                        .long("prune")
                        .help("Remove unrelated or leftover .so files")
                        .action(ArgAction::SetTrue),
                )
                .arg(
                    Arg::new("verbose")
                        .short('v')
                        .long("verbose")
                        .help("Print information about plugins being set up or skipped")
                        .action(ArgAction::SetTrue),
                ),
        )
        .subcommand(
            Command::new("set")
                .about("Change the yabridge path (advanced)")
                .display_order(200)
                .arg_required_else_help(true)
                .arg(
                    Arg::new("path")
                        .long("path")
                        .help(
                            "Path to the directory containing \
                             'libyabridge-chainloader-{clap,vst2,vst3}.so'",
                        )
                        .long_help(
                            "Path to the directory containing \
                             'libyabridge-chainloader-{clap,vst2,vst3}.so'. If this is not set, \
                             then yabridgectl will look in both '/usr/lib' and \
                             '~/.local/share/yabridge' by default.",
                        )
                        .value_parser(parse_directory_path)
                        .conflicts_with("path_auto"),
                )
                .arg(
                    Arg::new("path_auto")
                        .long("path-auto")
                        .help("Automatically locate yabridge's files")
                        .long_help(
                            "Automatically locate yabridge's files. This can be used after \
                             manually setting a path with the '--path' option to revert back to \
                             the default auto detection behaviour.",
                        )
                        .action(ArgAction::SetTrue),
                )
                .arg(
                    Arg::new("vst2_location")
                        .long("vst2-location")
                        .help("Where to set up VST2 plugins")
                        .long_help("Where to set up VST2 plugins.")
                        .value_parser(value_parser!(Vst2Location)),
                )
                .arg(
                    Arg::new("no_verify")
                        .long("no-verify")
                        .help("Always skip post-installation setup checks")
                        .long_help(
                            "Always skip post-installation setup checks. This can be set \
                             temporarily by passing the '--no-verify' option to 'yabridgectl \
                             sync'.",
                        )
                        .value_parser(value_parser!(bool)),
                ),
        )
        .subcommand(
            Command::new("blacklist")
                .about("Manage the indexing blacklist (advanced)")
                .display_order(201)
                .subcommand_required(true)
                .arg_required_else_help(true)
                .long_about(
                    "Manage the indexing blacklist (advanced)\n\nThis lets you skip over \
                     individual files and entire directories in the indexing process. You most \
                     likely won't have to use this feature.",
                )
                .subcommand(
                    Command::new("add")
                        .about("Add a path to the blacklist")
                        .display_order(1)
                        .arg(
                            Arg::new("path")
                                .help("Path to a file or a directory")
                                .value_parser(parse_path)
                                .required(true),
                        ),
                )
                .subcommand(
                    Command::new("rm")
                        .about("Remove a path from the blacklist")
                        .display_order(2)
                        .arg(
                            Arg::new("path")
                                .help("Path to a previously added file or directory")
                                .value_parser(parse_path_from_set(blacklist_entries))
                                .required(true),
                        ),
                )
                .subcommand(
                    Command::new("list")
                        .about("List the blacklisted paths")
                        .display_order(3),
                )
                .subcommand(
                    Command::new("clear")
                        .about("Clear the entire blacklist")
                        .display_order(4),
                ),
        )
        .get_matches();

    // We're calling canonicalize when adding and setting paths since relative paths would cause
    // some weird behaviour. There's no built-in way to make relative paths absoltue without
    // resolving symlinks, but I don't think this will cause any issues.
    //
    // https://github.com/rust-lang/rust/issues/59117
    match matches.subcommand() {
        Some(("add", options)) => actions::add_directory(
            &mut config,
            options.get_one::<PathBuf>("path").unwrap().canonicalize()?,
        ),
        Some(("rm", options)) => {
            actions::remove_directory(
                &mut config,
                // The parser already ensures that this value exists in the plugin locations set
                options.get_one::<PathBuf>("path").unwrap(),
            )
        }
        Some(("list", _)) => actions::list_directories(&config),
        Some(("status", _)) => actions::show_status(&config),
        Some(("sync", options)) => actions::do_sync(
            &mut config,
            &actions::SyncOptions {
                force: options.get_flag("force"),
                no_verify: options.get_flag("no-verify"),
                prune: options.get_flag("prune"),
                verbose: options.get_flag("verbose"),
            },
        ),
        Some(("set", options)) => actions::set_settings(
            &mut config,
            &actions::SetOptions {
                // We've already verified that the path is valid, so we should only be getting
                // errors for missing arguments
                path: options
                    .get_one::<PathBuf>("path")
                    .and_then(|path| path.canonicalize().ok()),
                path_auto: options.get_flag("path_auto"),
                vst2_location: options.get_one::<Vst2Location>("vst2_location").copied(),
                no_verify: options.get_one::<bool>("no_verify").copied(),
            },
        ),
        Some(("blacklist", blacklist)) => match blacklist.subcommand() {
            Some(("add", options)) => actions::blacklist::add_path(
                &mut config,
                options.get_one::<PathBuf>("path").unwrap().canonicalize()?,
            ),
            Some(("rm", options)) => {
                actions::blacklist::remove_path(
                    &mut config,
                    // The parser already ensures that this value exists in the plugin locations set
                    options.get_one::<PathBuf>("path").unwrap(),
                )
            }
            Some(("list", _)) => actions::blacklist::list_paths(&config),
            Some(("clear", _)) => actions::blacklist::clear(&mut config),
            _ => unreachable!(),
        },
        _ => unreachable!(),
    }
}

/// Verify that a path exists. Used for validating arguments.
fn parse_path(path: &str) -> Result<PathBuf, String> {
    let path = Path::new(path);

    if path.exists() {
        Ok(path.to_owned())
    } else {
        Err(String::from("File or directory could not be found."))
    }
}

/// [`parse_path()`], but for directories or symlinks to directories.
fn parse_directory_path(path: &str) -> Result<PathBuf, String> {
    let path = Path::new(path);

    if path.exists() {
        if path.is_dir() {
            Ok(path.to_owned())
        } else {
            Err(String::from("Path is not a directory."))
        }
    } else {
        Err(String::from("Directory could not be found."))
    }
}

/// Constructs a parser that checks if `path` is in the set of locations, and returns an absolute
/// path to the location if it is. This is similar to using `Arg::possible_values()`, except that it
/// also tries to resolve symlinks, relative paths, and other common variations. If the path is
/// relative, we will try to resolve as much of it as possible (in case the referred to file doesn't
/// exist anymore). We don't iteratively try to resolve symlinks until a candidate matches a path in
/// `candidates`, but this can match a relative path to a symlink that's in the paths list.
fn parse_path_from_set(candidates: HashSet<PathBuf>) -> impl TypedValueParser<Value = PathBuf> {
    move |value: &str| -> Result<PathBuf, String> {
        // This path does not need to exist, since a plugin location may no longer exist on disk
        let path = Path::new(value);
        let absolute_path = if path.is_absolute() {
            path.to_path_buf()
        } else {
            // This absolute absolute_path is also needed for the `utils::normalize_path()` below
            std::env::current_dir()
                .expect("Couldn't get current directory")
                .join(path)
        };

        // If the absolute path is not in the plugin locations verbatim, we'll try a couple
        // different variations
        if let Some(matching_path) = candidates.get(absolute_path.as_path()) {
            return Ok(matching_path.to_path_buf());
        }

        // This will include a trailing slash if `path` was `.`. All paths entered through
        // yabridgectl will be cannonicalized and won't contain a trailing slash, but we'll try both
        // variants anyways just in case someone edited the config file.
        let normalized_path = util::normalize_path(absolute_path.as_path());

        // Is there a nicer way to strip trailing slashes with the standard library?
        let normalized_path_str = normalized_path
            .to_str()
            .expect("Input path contains invalid characters");
        let normalized_path_without_slash = if normalized_path_str.ends_with('/') {
            Path::new(normalized_path_str.trim_end_matches('/'))
        } else {
            normalized_path.as_path()
        };
        // This ia bit of a hack, but it works
        let normalized_path_with_slash = normalized_path.join("");

        if let Some(found_path) = candidates
            .get(normalized_path_without_slash)
            .or_else(|| candidates.get(normalized_path_with_slash.as_path()))
        {
            return Ok(found_path.to_path_buf());
        }

        // There's sadly no way to use clap's normal error formatting for possible values here since
        // parts of the API are not exposed
        Err(format!(
            "Not a known path.\n\n       Possible options are: {}",
            format!("{:?}", candidates).green()
        ))
    }
}
