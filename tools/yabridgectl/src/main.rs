// yabridge: a Wine VST bridge
// Copyright (C) 2020  Robbert van der Helm
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
use std::path::{Path, PathBuf};

use crate::config::Config;

mod actions;
mod config;
mod files;
mod utils;

fn main() -> Result<()> {
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
            App::new("add").about("Add a plugin install location").arg(
                Arg::with_name("path")
                    .about("Path to a directory containing Windows VST plugins")
                    .validator(validate_path)
                    .takes_value(true)
                    .required(true),
            ),
        )
        .subcommand(
            App::new("rm")
                .about("Remove a plugin install location")
                .arg(
                    Arg::with_name("path")
                        .about("Path to a previously added directory")
                        .possible_values(&plugin_directories)
                        .takes_value(true)
                        .required(true),
                ),
        )
        .subcommand(App::new("list").about("List the plugin install locations"))
        .subcommand(App::new("status").about("Show the installation status for all plugins"))
        .subcommand(
            App::new("set")
                .about("Change installation method or yabridge path")
                .setting(AppSettings::ArgRequiredElseHelp)
                .arg(
                    Arg::with_name("method")
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
                    Arg::with_name("path")
                        .long("path")
                        .about("Path to the directory containing 'libyabridge.so'")
                        .long_about(
                            "Path to the directory containing 'libyabridge.so'. If this is \
                             not set, then yabridgectl will look in both '/usr/lib' and \
                             '~/.local/share/yabridge' by default.",
                        )
                        .validator(validate_path)
                        .takes_value(true),
                ),
        )
        .subcommand(
            App::new("sync")
                .about("Set up or update yabridge for all plugins")
                .arg(
                    Arg::with_name("no-verify")
                        .short('n')
                        .long("no-verify")
                        .about("Skip post-installation setup checks"),
                )
                .arg(
                    Arg::with_name("prune")
                        .short('p')
                        .long("prune")
                        .about("Remove unrelated or leftover .so files"),
                )
                .arg(
                    Arg::with_name("verbose")
                        .short('v')
                        .long("verbose")
                        .about("Print information about plugins being set up or skipped"),
                ),
        )
        .get_matches();

    // We're calling canonicalize when adding and setting paths since relative paths would cause
    // some weird behaviour. There's no built-in way to make relative paths absoltue without
    // resolving symlinks, but I don't think this will cause any issues.
    //
    // https://github.com/rust-lang/rust/issues/59117
    match matches.subcommand() {
        ("add", Some(options)) => actions::add_directory(
            &mut config,
            options
                .value_of_t_or_exit::<PathBuf>("path")
                .canonicalize()?,
        ),
        ("rm", Some(options)) => actions::remove_directory(
            &mut config,
            &options
                .value_of_t_or_exit::<PathBuf>("path")
                .canonicalize()?,
        ),
        ("list", _) => actions::list_directories(&config),
        ("status", _) => actions::show_status(&config),
        ("set", Some(options)) => actions::set_settings(
            &mut config,
            &actions::SetOptions {
                method: options.value_of("method"),
                // We've already verified that the path is valid, so we should only be getting
                // errors for missing arguments
                path: options
                    .value_of_t::<PathBuf>("path")
                    .ok()
                    .and_then(|path| path.canonicalize().ok()),
            },
        ),
        ("sync", Some(options)) => actions::do_sync(
            &mut config,
            &actions::SyncOptions {
                no_verify: options.is_present("no-verify"),
                prune: options.is_present("prune"),
                verbose: options.is_present("verbose"),
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
