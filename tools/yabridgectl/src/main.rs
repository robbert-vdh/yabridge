// yabridge: a Wine VST bridge
// Copyright (C) 2020-2021 Robbert van der Helm
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
use clap::{app_from_crate, App, AppSettings, Arg};
use colored::Colorize;
use std::env;
use std::path::{Path, PathBuf};

use crate::config::Config;

mod actions;
mod config;
mod files;
mod utils;

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

    // Used for validation in `yabridgectl rm <path>`
    let plugin_directories: Vec<&str> = config
        .plugin_dirs
        .iter()
        .map(|path| path.to_str().expect("Path contains invalid unicode"))
        .collect();

    let matches = app_from_crate!()
        .setting(AppSettings::SubcommandRequiredElseHelp)
        .subcommand(
            App::new("add").about("Add a plugin install location").display_order(1).arg(
                Arg::new("path")
                    .about("Path to a directory containing Windows VST plugins")
                    .validator(validate_path)
                    .takes_value(true)
                    .required(true),
            ),
        )
        .subcommand(
            App::new("rm")
                .about("Remove a plugin install location")
                .display_order(2)
                .arg(
                    Arg::new("path")
                        .about("Path to a previously added directory")
                        .possible_values(&plugin_directories)
                        .takes_value(true)
                        .required(true),
                ),
        )
        .subcommand(App::new("list").about("List the plugin install locations").display_order(3))
        .subcommand(App::new("status").about("Show the installation status for all plugins").display_order(4))
        .subcommand(
            App::new("sync")
                .about("Set up or update yabridge for all plugins")
                .display_order(100)
                .arg(
                    Arg::new("force")
                        .short('f')
                        .long("force")
                        .about("Always update files, even not necessary"),
                )
                .arg(
                    Arg::new("no-verify")
                        .short('n')
                        .long("no-verify")
                        .about("Skip post-installation setup checks"),
                )
                .arg(
                    Arg::new("prune")
                        .short('p')
                        .long("prune")
                        .about("Remove unrelated or leftover .so files"),
                )
                .arg(
                    Arg::new("verbose")
                        .short('v')
                        .long("verbose")
                        .about("Print information about plugins being set up or skipped"),
                ),
        )
        .subcommand(
            App::new("set")
                .about("Change the installation method or yabridge path (advanced)")
                .display_order(200)
                .setting(AppSettings::ArgRequiredElseHelp)
                .arg(
                    Arg::new("method")
                        .long("method")
                        .about("The installation method to use")
                        .long_about(&format!(
                            "The installation method to use. \
                             '{}' works in every situation but it requires you to modify your PATH \
                             environment variable so yabridge is able to find 'yabridge-host.exe'. \
                             'yabridgectl sync' whenever you update yabridge. You'll also have to \
                             rerun 'yabridgectl sync' whenever you update yabridge. \
                             '{}' only works for hosts that support individually sandboxed plugins \
                             such as Bitwig Studio, but it does not require setting environment \
                             variables or to manual updates.",
                            "copy".bright_white(),
                            "symlink".bright_white()
                        ))
                        .setting(clap::ArgSettings::NextLineHelp)
                        .possible_values(&["copy", "symlink"])
                        .takes_value(true),
                )
                .arg(
                    Arg::new("path")
                        .long("path")
                        .about("Path to the directory containing 'libyabridge-{vst2,vst3}.so'")
                        .long_about(
                            "Path to the directory containing 'libyabridge-{vst2,vst3}.so'. If this \
                             is not set, then yabridgectl will look in both '/usr/lib' and \
                             '~/.local/share/yabridge' by default.",
                        )
                        .validator(validate_path)
                        .takes_value(true).conflicts_with("path_auto"),
                )
                .arg(
                    Arg::new("path_auto")
                        .long("path-auto")
                        .about("Automatically locate yabridge's files")
                        .long_about(
                            "Automatically locate yabridge's files. This can be used after manually \
                             setting a path with the '--path' option to revert back to the default \
                             auto detection behaviour.",
                        ),
                ).arg(
                    Arg::new("no_verify")
                        .long("no-verify")
                        .about("Always skip post-installation setup checks")
                        .long_about(
                            "Always skip post-installation setup checks. This can be set temporarily \
                             by passing the '--no-verify' option to 'yabridgectl sync'.",
                        )
                        .possible_values(&["true", "false"])
                        .takes_value(true),
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
            options
                .value_of_t_or_exit::<PathBuf>("path")
                .canonicalize()?,
        ),
        Some(("rm", options)) => {
            actions::remove_directory(&mut config, &options.value_of_t_or_exit::<PathBuf>("path"))
        }
        Some(("list", _)) => actions::list_directories(&config),
        Some(("status", _)) => actions::show_status(&config),
        Some(("sync", options)) => actions::do_sync(
            &mut config,
            &actions::SyncOptions {
                force: options.is_present("force"),
                no_verify: options.is_present("no-verify"),
                prune: options.is_present("prune"),
                verbose: options.is_present("verbose"),
            },
        ),
        Some(("set", options)) => actions::set_settings(
            &mut config,
            &actions::SetOptions {
                method: options.value_of("method"),
                // We've already verified that the path is valid, so we should only be getting
                // errors for missing arguments
                path: options
                    .value_of_t::<PathBuf>("path")
                    .ok()
                    .and_then(|path| path.canonicalize().ok()),
                path_auto: options.is_present("path_auto"),
                no_verify: options.value_of("no_verify").map(|value| value == "true"),
            },
        ),
        _ => unreachable!(),
    }
}

/// Verify that a path exists, used for validating arguments.
fn validate_path(path: &str) -> Result<(), String> {
    let path = Path::new(path);

    if path.exists() {
        Ok(())
    } else {
        Err(format!(
            "File or directory '{}' could not be found",
            path.display()
        ))
    }
}
