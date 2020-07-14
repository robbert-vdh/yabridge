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

use clap::{app_from_crate, App, AppSettings, Arg};
use config::Config;
use std::path::{Path, PathBuf};
use std::process::exit;

mod config;
mod files;

// TODO: Add the different `yabridgectl set` options
// TODO: Add `yabridgectl status`
// TODO: Add `yabridgectl sync`
// TODO: Naming and descriptions could be made clearer
// TODO: When creating copies, check whether `yabridge-host.exe` is in the PATH for the login shell
// TODO: Also give a good error if winedump cannot be found
// TODO: Check for left over files when removign directory
// TODO: Warn about left over files if not using --prune
// TODO: Reward parts of the readme
// TODO: Record .dll files processed, .dll files skipped and orphan .so files. Print a summary of
//       the work done, and allow a --verbose option to print everything.

fn main() {
    let mut config = match Config::read() {
        Ok(config) => config,
        Err(err) => {
            eprintln!("Error while reading config:\n\n{}", err);
            std::process::exit(1);
        }
    };

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
                        .about("Path to a directory")
                        .possible_values(&plugin_directories)
                        .takes_value(true)
                        .required(true),
                ),
        )
        .subcommand(App::new("list").about("List the plugin install locations"))
        .get_matches();

    match matches.subcommand() {
        ("add", Some(options)) => add_directory(&mut config, options.value_of_t_or_exit("path")),
        ("rm", Some(options)) => {
            remove_directory(&mut config, &options.value_of_t_or_exit::<PathBuf>("path"))
        }
        ("list", _) => list_directories(&config),
        _ => unreachable!(),
    }
}

/// Add a direcotry to the plugin locations. Duplicates get ignord because we're using ordered sets.
fn add_directory(config: &mut Config, path: PathBuf) {
    config.plugin_dirs.insert(path);
    if let Err(err) = config.write() {
        eprintln!("Error while writing config file: {}", err);
        exit(1);
    };
}

/// Remove a direcotry to the plugin locations. The path is assumed to be part of
/// `config.plugin_dirs`, otherwise this si silently ignored.
fn remove_directory(config: &mut Config, path: &Path) {
    // We've already verified that this path is in `config.plugin_dirs`
    config.plugin_dirs.remove(path);
    if let Err(err) = config.write() {
        eprintln!("Error while writing config file: {}", err);
        exit(1);
    };
}

/// List the plugin locations.
fn list_directories(config: &Config) {
    for directory in &config.plugin_dirs {
        println!("{}", directory.display());
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
