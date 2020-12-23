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

//! Handlers for the subcommands, just to keep `main.rs` clean.

use anyhow::{Context, Result};
use colored::Colorize;
use std::fs;
use std::path::{Path, PathBuf};

use crate::config::{Config, InstallationMethod, YabridgeFiles};
use crate::files;
use crate::files::NativeSoFile;
use crate::utils;
use crate::utils::{verify_path_setup, verify_wine_setup};

/// Add a direcotry to the plugin locations. Duplicates get ignord because we're using ordered sets.
pub fn add_directory(config: &mut Config, path: PathBuf) -> Result<()> {
    config.plugin_dirs.insert(path);
    config.write()
}

/// Remove a direcotry to the plugin locations. The path is assumed to be part of
/// `config.plugin_dirs`, otherwise this si silently ignored.
pub fn remove_directory(config: &mut Config, path: &Path) -> Result<()> {
    // We've already verified that this path is in `config.plugin_dirs`
    // XXS: Would it be a good idea to warn about leftover .so files?
    config.plugin_dirs.remove(path);
    config.write()?;

    // Ask the user to remove any leftover files to prevent possible future problems and out of date
    // copies
    let orphan_files = files::index_so_files(path);
    if !orphan_files.is_empty() {
        println!(
            "Warning: Found {} leftover .so files still in this directory:",
            orphan_files.len()
        );

        for file in &orphan_files {
            println!("- {}", file.path().display());
        }

        match promptly::prompt_opt::<String, &str>(
            "\nWould you like to remove these files? Entering anything other than YES will leave \
             these files intact",
        ) {
            Ok(Some(answer)) if answer == "YES" => {
                for file in &orphan_files {
                    utils::remove_file(file.path())?;
                }

                println!("\nRemoved {} files", orphan_files.len());
            }
            _ => {}
        }
    }

    Ok(())
}

/// List the plugin locations.
pub fn list_directories(config: &Config) -> Result<()> {
    for directory in &config.plugin_dirs {
        println!("{}", directory.display());
    }

    Ok(())
}

/// Print the current configuration and the installation status for all found plugins.
pub fn show_status(config: &Config) -> Result<()> {
    let results = config
        .index_directories()
        .context("Failure while searching for plugins")?;

    println!(
        "yabridge path: {}",
        config
            .yabridge_home
            .as_ref()
            .map(|path| format!("'{}'", path.display()))
            .unwrap_or_else(|| String::from("<auto>"))
    );

    match config.files() {
        Ok(files) => {
            println!(
                "libyabridge-vst2.so: '{}'",
                files.libyabridge_vst2.display()
            );
            println!(
                "libyabridge-vst3.so: {}\n",
                files
                    .libyabridge_vst3
                    .map(|path| format!("'{}'", path.display()))
                    .unwrap_or_else(|| "<not found>".red().to_string())
            );
        }
        Err(err) => {
            println!("Could not find yabridge's files files: {}\n", err);
        }
    }

    println!("installation method: {}", config.method);
    for (path, search_results) in results {
        println!("\n{}:", path.display());

        for (plugin, status) in search_results.installation_status() {
            let status_str = match status {
                Some(NativeSoFile::Regular(_)) => "copy".green(),
                Some(NativeSoFile::Symlink(_)) => "symlink".green(),
                None => "not installed".red(),
            };

            println!("  {} :: {}", plugin.display(), status_str);
        }
    }

    Ok(())
}

/// Options passed to `yabridgectl set`, see `main()` for the definitions of these options.
pub struct SetOptions<'a> {
    pub method: Option<&'a str>,
    pub path: Option<PathBuf>,
}

/// Change configuration settings. The actual options are defined in the clap [app](clap::App).
pub fn set_settings(config: &mut Config, options: &SetOptions) -> Result<()> {
    match options.method {
        Some("copy") => config.method = InstallationMethod::Copy,
        Some("symlink") => config.method = InstallationMethod::Symlink,
        Some(s) => unimplemented!("Unexpected installation method '{}'", s),
        None => (),
    }

    if let Some(path) = &options.path {
        config.yabridge_home = Some(path.clone());
    }

    config.write()
}

/// Options passed to `yabridgectl sync`, see `main()` for the definitions of these options.
pub struct SyncOptions {
    pub force: bool,
    pub no_verify: bool,
    pub prune: bool,
    pub verbose: bool,
}

/// Set up yabridge for all Windows VST2 plugins in the plugin directories. Will also remove orphan
/// `.so` files if the prune option is set.
pub fn do_sync(config: &mut Config, options: &SyncOptions) -> Result<()> {
    let files: YabridgeFiles = config.files()?;
    let libyabridge_vst2_hash = utils::hash_file(&files.libyabridge_vst2)?;
    println!("Using '{}'\n", files.libyabridge_vst2.display());

    let results = config
        .index_directories()
        .context("Failure while searching for plugins")?;

    // Keep track of some global statistics
    let mut num_installed = 0;
    let mut num_new = 0;
    let mut skipped_dll_files: Vec<PathBuf> = Vec::new();
    let mut orphan_so_files: Vec<NativeSoFile> = Vec::new();
    for (path, search_results) in results {
        num_installed += search_results.vst2_files.len();
        orphan_so_files.extend(search_results.orphans().into_iter().cloned());
        skipped_dll_files.extend(search_results.skipped_files);

        if options.verbose {
            println!("{}:", path.display());
        }
        for plugin in search_results.vst2_files {
            let target_path = plugin.with_extension("so");

            // We'll only recreate existing files when updating yabridge, when switching between the
            // symlink and copy installation methods, or when the `force` option is set. If the
            // target file already exists and does not require updating, we'll just skip the file
            // since some DAWs will otherwise unnecessarily reindex the file.
            // We check `std::fs::symlink_metadata` instead of `Path::exists()` because the latter
            // reports false for broken symlinks.
            if let Ok(metadata) = fs::symlink_metadata(&target_path) {
                match (options.force, &config.method) {
                    (false, InstallationMethod::Copy) => {
                        // If the target file is already a real file (not a symlink) and its hash is
                        // the same as the `libyabridge-vst2.so` file we're trying to copy there,
                        // then we don't have to do anything
                        if metadata.file_type().is_file()
                            && utils::hash_file(&target_path)? == libyabridge_vst2_hash
                        {
                            continue;
                        }
                    }
                    (false, InstallationMethod::Symlink) => {
                        // If the target file is already a symlink to `libyabridge-vst2.so`, then we
                        // can skip this file
                        if metadata.file_type().is_symlink()
                            && target_path.read_link()? == files.libyabridge_vst2
                        {
                            continue;
                        }
                    }
                    // With the force option we always want to recreate existing .so files
                    (true, _) => (),
                }

                utils::remove_file(&target_path)?;
            };

            // Since we skip some files, we'll also keep track of how many new file we've actually
            // set up
            num_new += 1;
            match config.method {
                InstallationMethod::Copy => {
                    utils::copy(&files.libyabridge_vst2, &target_path)?;
                }
                InstallationMethod::Symlink => {
                    utils::symlink(&files.libyabridge_vst2, &target_path)?;
                }
            }

            if options.verbose {
                println!("  {}", plugin.display());
            }
        }
        if options.verbose {
            println!();
        }
    }

    // We'll print the skipped files all at once to prevetn clutter
    let num_skipped_files = skipped_dll_files.len();
    if options.verbose && !skipped_dll_files.is_empty() {
        println!("Skipped files:");
        for path in skipped_dll_files {
            println!("- {}", path.display());
        }
        println!();
    }

    // Always warn about leftover files sicne those might cause warnings or errors when a VST host
    // tries to load them
    if !orphan_so_files.is_empty() {
        if options.prune {
            println!("Removing {} leftover .so files:", orphan_so_files.len());
        } else {
            println!(
                "Found {} leftover .so files, rerun with the '--prune' option to remove them:",
                orphan_so_files.len()
            );
        }

        for file in orphan_so_files {
            let path = file.path();

            println!("- {}", path.display());
            if options.prune {
                utils::remove_file(path)?;
            }
        }

        println!();
    }

    println!(
        "Finished setting up {} plugins using {} ({} new), skipped {} non-plugin .dll files",
        num_installed,
        config.method.plural_name(),
        num_new,
        num_skipped_files
    );

    if options.no_verify {
        return Ok(());
    }

    if config.method == InstallationMethod::Copy {
        verify_path_setup(config)?;
    }

    // This check is only performed once per combination of Wine and yabridge versions
    verify_wine_setup(config)?;

    Ok(())
}
