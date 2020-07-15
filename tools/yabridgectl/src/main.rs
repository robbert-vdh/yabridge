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

use clap::{app_from_crate, App, AppSettings, Arg, ArgMatches};
use colored::Colorize;
use std::fs;
use std::os::unix::fs::symlink;
use std::path::{Path, PathBuf};
use std::process::exit;

use crate::config::{Config, InstallationMethod};
use crate::files::FoundFile;

mod config;
mod files;

// TODO: Naming and descriptions could be made clearer
// TODO: When creating copies, check whether `yabridge-host.exe` is in the PATH for the login shell
// TODO: Check for left over files when removing directory
// TODO: Reward parts of the readme

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
        .subcommand(App::new("status").about("Show the installation status for all plugins"))
        .subcommand(
            App::new("set")
                .about("Change installation method or yabridge path")
                .setting(AppSettings::ArgRequiredElseHelp)
                .arg(
                    Arg::with_name("method")
                        .long("method")
                        .about("The installation method to use")
                        .long_about("The installation method to use.")
                        .possible_values(&["copy", "symlink"])
                        .takes_value(true),
                )
                .arg(
                    Arg::with_name("path")
                        .long("path")
                        .about("Path to the directory containing 'libyabridge.so'")
                        .long_about("Path to the directory containing 'libyabridge.so'. If this is not set, then yabridgectl will look in both '/usr/lib' and '~/.local/share/yabridge' by default.")
                        .validator(validate_path)
                        .takes_value(true),
                ),
        )
        .subcommand(
            App::new("sync")
                .about("Set up or update yabridge for all plugins")
                .arg(
                    Arg::with_name("prune")
                        .short('p')
                        .long("prune")
                        .about("Remove unrelated or leftover '.so' files"),
                )
                .arg(
                    Arg::with_name("verbose")
                        .short('v')
                        .long("verbose")
                        .about("Print information about plugins being set up or skipped"),
                ),
        )
        .get_matches();

    match matches.subcommand() {
        ("add", Some(options)) => add_directory(&mut config, options.value_of_t_or_exit("path")),
        ("rm", Some(options)) => {
            remove_directory(&mut config, &options.value_of_t_or_exit::<PathBuf>("path"))
        }
        ("list", _) => list_directories(&config),
        ("status", _) => show_status(&config),
        ("set", Some(options)) => set_settings(&mut config, options),
        ("sync", Some(options)) => do_sync(
            &config,
            options.is_present("prune"),
            options.is_present("verbose"),
        ),
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
    // XXS: Would it be a good idea to warn about leftover .so files?
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

/// Print the current configuration and the installation status for all found plugins.
fn show_status(config: &Config) {
    let results = match config.index_directories() {
        Ok(results) => results,
        Err(err) => {
            eprintln!("Error while searching for plugins: {}", err);
            exit(1);
        }
    };

    println!(
        "yabridge path: {}",
        config
            .yabridge_home
            .as_ref()
            .map(|path| format!("'{}'", path.display()))
            .unwrap_or_else(|| String::from("<auto>"))
    );
    println!(
        "libyabridge.so: {}",
        config
            .libyabridge()
            .map(|path| format!("'{}'", path.display()))
            .unwrap_or_else(|_| format!("{}", "<not found>".red()))
    );
    println!("installation method: {}", config.method);

    for (path, search_results) in results {
        println!("\n{}:", path.display());

        for (plugin, status) in search_results.installation_status() {
            let status_str = match status {
                Some(FoundFile::Regular(_)) => "copy".green(),
                Some(FoundFile::Symlink(_)) => "symlink".green(),
                None => "not installed".red(),
            };

            println!("  {} :: {}", plugin.display(), status_str);
        }
    }
}

/// Change configuration settings. The actual options are defined in the clap [app](clap::App).
fn set_settings(config: &mut Config, options: &ArgMatches) {
    match options.value_of("method") {
        Some("copy") => config.method = InstallationMethod::Copy,
        Some("symlink") => config.method = InstallationMethod::Symlink,
        Some(s) => unimplemented!("Unexpected installation method '{}'", s),
        None => (),
    }

    match options.value_of_t("path") {
        Ok(path) => config.yabridge_home = Some(path),
        Err(clap::Error {
            kind: clap::ErrorKind::ArgumentNotFound,
            ..
        }) => (),
        // I don't think we can get any parsing errors here since we already validated that the
        // argument has to be a valid path, but you never know
        Err(err) => err.exit(),
    }

    if let Err(err) = config.write() {
        eprintln!("Error while writing config file: {}", err);
        exit(1);
    };
}

/// Set up yabridge for all Windows VST2 plugins in the plugin directories. Will also remove orphan
/// `.so` files if the prune option is set.
fn do_sync(config: &Config, prune: bool, verbose: bool) {
    let libyabridge_path = match config.libyabridge() {
        Ok(path) => {
            println!("Using '{}'\n", path.display());
            path
        }
        Err(err) => {
            // The error messages here are already formatted
            eprintln!("{}", err);
            exit(1);
        }
    };

    let results = match config.index_directories() {
        Ok(results) => results,
        Err(err) => {
            eprintln!("Error while searching for plugins: {}", err);
            exit(1);
        }
    };

    // Keep track of some global statistics
    let mut num_installed = 0;
    let mut skipped_dll_files: Vec<PathBuf> = Vec::new();
    let mut orphan_so_files: Vec<FoundFile> = Vec::new();
    for (path, search_results) in results {
        num_installed += search_results.vst2_files.len();
        orphan_so_files.extend(search_results.orphans().into_iter().cloned());
        skipped_dll_files.extend(search_results.skipped_files);

        if verbose {
            println!("{}:", path.display());
        }
        for plugin in search_results.vst2_files {
            // If the target file already exists, we'll remove it first to prevent issues with
            // mixing symlinks and regular files
            let target_path = plugin.with_extension("so");
            if target_path.exists() {
                fs::remove_file(&target_path).unwrap_or_else(|err| {
                    eprintln!("Could not remove '{}': {}", target_path.display(), err);
                    exit(1);
                });
            }

            match config.method {
                InstallationMethod::Copy => {
                    fs::copy(&libyabridge_path, &target_path).unwrap_or_else(|err| {
                        eprintln!(
                            "Error copying '{}' to '{}': {}",
                            libyabridge_path.display(),
                            target_path.display(),
                            err
                        );
                        exit(1);
                    });
                }
                InstallationMethod::Symlink => {
                    symlink(&libyabridge_path, &target_path).unwrap_or_else(|err| {
                        eprintln!(
                            "Error symlinking '{}' to '{}': {}",
                            libyabridge_path.display(),
                            target_path.display(),
                            err
                        );
                        exit(1);
                    });
                }
            }

            if verbose {
                println!("  {}", plugin.display());
            }
        }
        if verbose {
            println!();
        }
    }

    // We'll print the skipped files all at once to prevetn clutter
    let num_skipped_files = skipped_dll_files.len();
    if verbose && !skipped_dll_files.is_empty() {
        println!("Skipped files:");
        for path in skipped_dll_files {
            println!("- {}", path.display());
        }
        println!();
    }

    // Always warn about leftover files sicne those might cause warnings or errors when a VST host
    // tries to load them
    if !orphan_so_files.is_empty() {
        if prune {
            println!("Removing {} leftover '.so' file(s):", orphan_so_files.len());
        } else {
            println!(
                "Found {} leftover '.so' file(s), rerun with the '--prune' option to remove them:",
                orphan_so_files.len()
            );
        }

        for file in orphan_so_files {
            let path = file.path();

            println!("- {}", path.display());
            if prune {
                fs::remove_file(path).unwrap_or_else(|err| {
                    eprintln!("Error while trying to remove '{}': {}", path.display(), err);
                    exit(1);
                });
            }
        }

        println!();
    }

    println!(
        "Finished setting up {} plugins using {}, skipped {} non-plugin '.dll' files.",
        num_installed,
        config.method.plural(),
        num_skipped_files
    )
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
