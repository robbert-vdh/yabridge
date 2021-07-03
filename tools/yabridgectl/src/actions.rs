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

//! Handlers for the subcommands, just to keep `main.rs` clean.

use anyhow::{Context, Result};
use colored::Colorize;
use std::collections::{HashMap, HashSet};
use std::fs;
use std::path::{Path, PathBuf};
use walkdir::WalkDir;

use crate::config::{yabridge_vst3_home, Config, InstallationMethod, YabridgeFiles};
use crate::files::{self, NativeFile, Plugin, Vst2Plugin};
use crate::utils::{self, get_file_type};
use crate::utils::{verify_path_setup, verify_wine_setup};

pub mod blacklist;

/// Add a direcotry to the plugin locations. Duplicates get ignord because we're using ordered sets.
pub fn add_directory(config: &mut Config, path: PathBuf) -> Result<()> {
    config.plugin_dirs.insert(path);
    config.write()
}

/// Remove a direcotry to the plugin locations. The path is assumed to be part of
/// `config.plugin_dirs`, otherwise this is silently ignored.
pub fn remove_directory(config: &mut Config, path: &Path) -> Result<()> {
    // We've already verified that this path is in `config.plugin_dirs`
    config.plugin_dirs.remove(path);
    config.write()?;

    // Ask the user to remove any leftover files to prevent possible future problems and out of date
    // copies
    let orphan_files = files::index(path, &HashSet::new()).so_files;
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
        .search_directories()
        .context("Failure while searching for plugins")?;

    println!(
        "yabridge path: {}",
        config
            .yabridge_home
            .as_ref()
            .map(|path| format!("'{}'", path.display()))
            .unwrap_or_else(|| String::from("<auto>"))
    );

    let files = config.files();
    match &files {
        Ok(files) => {
            println!(
                "libyabridge-vst2.so: '{}'",
                files.libyabridge_vst2.display()
            );
            println!(
                "libyabridge-vst3.so: {}\n",
                files
                    .libyabridge_vst3
                    .as_ref()
                    .map(|(path, arch)| format!("'{}' ({})", path.display(), arch))
                    .unwrap_or_else(|| "<not found>".red().to_string())
            );
        }
        Err(err) => {
            println!("Could not find yabridge's files: {}\n", err);
        }
    }
    println!("installation method: {}", config.method);

    for (path, search_results) in results {
        // Always print these paths with trailing slashes for consistency's sake because paths can
        // be added both with and without a trailing slash
        println!("\n{}", path.join("").display());

        for (plugin_path, (plugin, status)) in
            search_results.installation_status(files.as_ref().ok())
        {
            let plugin_type = match plugin {
                Plugin::Vst2(Vst2Plugin { architecture, .. }) => {
                    format!("{}, {}", "VST2".cyan(), architecture)
                }
                Plugin::Vst3(module) => format!(
                    "{}, {}, {}",
                    "VST3".magenta(),
                    module.type_str(),
                    module.architecture
                ),
            };

            let status_str = match status {
                Some(NativeFile::Regular(_)) => "copy".green(),
                Some(NativeFile::Symlink(_)) => "symlink".green(),
                Some(NativeFile::Directory(_)) => "invalid".red(),
                None => "not yet installed".into(),
            };

            println!(
                "  {} :: {}, {}",
                plugin_path
                    .strip_prefix(path)
                    .unwrap_or(&plugin_path)
                    .display(),
                plugin_type,
                status_str
            );
        }
    }

    Ok(())
}

/// Options passed to `yabridgectl set`, see `main()` for the definitions of these options.
pub struct SetOptions<'a> {
    pub method: Option<&'a str>,
    pub path: Option<PathBuf>,
    pub path_auto: bool,
    pub no_verify: Option<bool>,
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

    if options.path_auto {
        config.yabridge_home = None;
    }

    if let Some(no_verify) = options.no_verify {
        config.no_verify = no_verify;
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
    let libyabridge_vst3_hash = match &files.libyabridge_vst3 {
        Some((path, _)) => Some(utils::hash_file(path)?),
        None => None,
    };

    if let Some((libyabridge_vst3_path, _)) = &files.libyabridge_vst3 {
        println!("Setting up VST2 and VST3 plugins using:");
        println!("- {}", files.libyabridge_vst2.display());
        println!("- {}\n", libyabridge_vst3_path.display());
    } else {
        println!("Setting up VST2 plugins using:");
        println!("- {}\n", files.libyabridge_vst2.display());
    }

    let results = config
        .search_directories()
        .context("Failure while searching for plugins")?;

    // Keep track of some global statistics
    // The plugin files we installed. This tracks copies of/symlinks to `libabyrdge-*.so` managed.
    // by yabridgectl. This could be optimized a bit so we wouldn't have to track everything, but
    // this makes everything much easier since we'll have to deal with things like a plugin
    // directory A containing a symlink to plugin directory B, as well as VST3 plugisn that come in
    // both x86 and x86_64 flavours.
    let mut managed_plugins: HashSet<PathBuf> = HashSet::new();
    // The plugins we created a new copy of `libyabridge-{vst2,vst3}.so` for. We don't touch these
    // files if they're already up to date to prevent hosts from unnecessarily rescanning the
    // plugins.
    let mut new_plugins: HashSet<PathBuf> = HashSet::new();
    // The files we skipped during the scan because they turned out to not be plugins
    let mut skipped_dll_files: Vec<PathBuf> = Vec::new();
    // `.so` files and unused VST3 modules we found during scanning that didn't have a corresponding
    // copy or symlink of `libyabridge-vst2.so`
    let mut orphan_files: Vec<NativeFile> = Vec::new();
    // Since VST3 bundles contain multiple files from multiple sources (native library files from
    // yabridge, and symlinks to Windows VST3 modules or bundles), cleaning up orphan VST3 files is
    // a bit more complicated. We want to clean both `.vst3` bundles that weren't used by anything
    // during the syncing process, so we'll keep track of which VST3 files we touched per-bundle. We
    // can then at the end remove all unkonwn bundles, and all unkonwn files within a bundle.
    let mut known_vst3_files: HashMap<PathBuf, HashSet<PathBuf>> = HashMap::new();
    for (path, search_results) in results {
        orphan_files.extend(search_results.vst2_orphans().into_iter().cloned());
        skipped_dll_files.extend(search_results.skipped_files);

        if options.verbose {
            // Always print these paths with trailing slashes for consistency's sake because paths
            // can be added both with and without a trailing slash
            println!("{}", path.join("").display());
        }

        for plugin in search_results.plugins {
            // If verbose mode is enabled we'll print the path to the plugin after setting it up
            let plugin_path: PathBuf = match plugin {
                // We'll set up the copies or symlinks for VST2 plugins
                Plugin::Vst2(Vst2Plugin {
                    path: plugin_path, ..
                }) => {
                    let target_path = plugin_path.with_extension("so");

                    // Since we skip some files, we'll also keep track of how many new file we've
                    // actually set up
                    if install_file(
                        options.force,
                        config.method,
                        &files.libyabridge_vst2,
                        Some(libyabridge_vst2_hash),
                        &target_path,
                    )? {
                        new_plugins.insert(target_path.clone());
                    }
                    managed_plugins.insert(target_path);

                    plugin_path.clone()
                }
                // And then create merged bundles for the VST3 plugins:
                // https://developer.steinberg.help/display/VST/Plug-in+Format+Structure#PluginFormatStructure-MergedBundle
                Plugin::Vst3(module) => {
                    // Only set up VST3 plugins when yabridge has been compiled with VST3 support
                    if libyabridge_vst3_hash.is_none() {
                        continue;
                    }

                    let target_bundle_home = module.target_bundle_home();
                    let target_native_module_path = module.target_native_module_path(Some(&files));
                    let target_windows_module_path = module.target_windows_module_path();

                    // 32-bit and 64-bit versions of the plugin can live inside of the same bundle),
                    // but it's not possible to use the exact same plugin from multiple Wine
                    // prefixes at the same time so we'll warn when that happens
                    let managed_vst3_bundle_files = known_vst3_files
                        .entry(target_bundle_home.clone())
                        .or_insert_with(HashSet::new);
                    if managed_vst3_bundle_files.contains(&target_windows_module_path) {
                        eprintln!(
                            "{}",
                            utils::wrap(&format!(
                            "{}: The {} version of '{}' has already been provided by another Wine \
                             prefix, skipping '{}'\n",
                            "WARNING".red(),
                            module.architecture,
                            module.target_bundle_home().display(),
                            module.original_module_path().display(),
                        ))
                        );

                        continue;
                    }

                    // We're building a merged VST3 bundle containing both a copy or symlink to
                    // `libyabridge-vst3.so` and the Windows VST3 plugin. The path to this native
                    // module will depend on whether `libyabridge-vst3.so` is a 32-bit or a 64-bit
                    // library file.
                    utils::create_dir_all(target_native_module_path.parent().unwrap())?;
                    if install_file(
                        options.force,
                        config.method,
                        &files.libyabridge_vst3.as_ref().unwrap().0,
                        libyabridge_vst3_hash,
                        &target_native_module_path,
                    )? {
                        new_plugins.insert(target_native_module_path.clone());
                    }
                    managed_plugins.insert(target_native_module_path.clone());
                    managed_vst3_bundle_files.insert(target_native_module_path);

                    // We'll then symlink the Windows VST3 module to that bundle to create a merged
                    // bundle: https://developer.steinberg.help/display/VST/Plug-in+Format+Structure#PluginFormatStructure-MergedBundle
                    utils::create_dir_all(target_windows_module_path.parent().unwrap())?;
                    install_file(
                        true,
                        InstallationMethod::Symlink,
                        &module.original_module_path(),
                        None,
                        &target_windows_module_path,
                    )?;
                    managed_vst3_bundle_files.insert(target_windows_module_path);

                    // If `module` is a bundle, then it may contain a `Resources` directory with
                    // screenshots and documentation
                    // TODO: Also symlink presets, but this is a bit more involved. See
                    //       https://developer.steinberg.help/display/VST/Preset+Locations
                    if let Some(original_resources_dir) = module.original_resources_dir() {
                        let target_resources_dir = module.target_resources_dir();

                        install_file(
                            false,
                            InstallationMethod::Symlink,
                            &original_resources_dir,
                            None,
                            &target_resources_dir,
                        )?;
                        managed_vst3_bundle_files.insert(target_resources_dir);
                    }

                    module.original_path().to_path_buf()
                }
            };

            if options.verbose {
                println!(
                    "  {}",
                    plugin_path
                        .strip_prefix(path)
                        .unwrap_or(&plugin_path)
                        .display()
                );
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

    // We want to remove both unmanaged VST3 bundles in `~/.vst3/yabridge` as well as
    // unmanged files within managed bundles. That's why we'll immediately filter out
    // kown files within VST3 bundles.
    // TODO: Move this elsewhere
    let installed_vst3_bundles = WalkDir::new(yabridge_vst3_home())
        .follow_links(true)
        .same_file_system(true)
        .into_iter()
        .filter_entry(|entry| entry.file_type().is_dir())
        .filter_map(|e| e.ok())
        .filter(|entry| {
            entry
                .path()
                .extension()
                .and_then(|extension| extension.to_str())
                == Some("vst3")
        });
    for bundle in installed_vst3_bundles {
        match known_vst3_files.get(bundle.path()) {
            None => orphan_files.push(NativeFile::Directory(bundle.path().to_owned())),
            Some(managed_vst3_bundle_files) => {
                // Find orphan files and symlinks within this bundle. We need this to be able to
                // switch between 32-bit and 64-bit versions of both yabridge and the Windows plugin
                orphan_files.extend(
                    WalkDir::new(bundle.path())
                        .follow_links(false)
                        .into_iter()
                        .filter_map(|e| e.ok())
                        .filter_map(|entry| {
                            let managed_file = managed_vst3_bundle_files.contains(entry.path());
                            match get_file_type(entry.path().to_owned()).unwrap() {
                                // Don't remove directories, since we're not tracking the
                                // directories within the bundle
                                NativeFile::Directory(_) => None,
                                unknown_file if !managed_file => Some(unknown_file),
                                _ => None,
                            }
                        }),
                );
            }
        }
    }

    // Always warn about leftover files since those might cause warnings or errors when a VST host
    // tries to load them
    if !orphan_files.is_empty() {
        let leftover_files_str = if orphan_files.len() == 1 {
            format!("{} leftover file", orphan_files.len())
        } else {
            format!("{} leftover files", orphan_files.len())
        };
        if options.prune {
            println!("Removing {}:", leftover_files_str);
        } else {
            println!(
                "Found {}, rerun with the '--prune' option to remove them:",
                leftover_files_str
            );
        }

        // TODO: Prune empty subdirectories
        for file in orphan_files {
            println!("- {}", file.path().display());
            if options.prune {
                match file {
                    NativeFile::Regular(path) | NativeFile::Symlink(path) => {
                        utils::remove_file(path)?;
                    }
                    NativeFile::Directory(path) => {
                        utils::remove_dir_all(path)?;
                    }
                }
            }
        }

        println!();
    }

    println!(
        "Finished setting up {} plugins using {} ({} new), skipped {} non-plugin .dll files",
        managed_plugins.len(),
        config.method.plural_name(),
        new_plugins.len(),
        num_skipped_files
    );

    // Skipping the post-installation seting checks can be done only for this invocation of
    // `yabridgectl sync`, or it can be skipped permanently through a config file option
    if options.no_verify || config.no_verify {
        return Ok(());
    }

    // The path setup is to make sure that the `libyabridge-{vst2,vst3}.so` copies can find
    // `yabridge-host.exe`
    if config.method == InstallationMethod::Copy {
        verify_path_setup(config)?;
    }

    // This check is only performed once per combination of Wine and yabridge versions
    verify_wine_setup(config)?;

    Ok(())
}

/// Create a copy or symlink of `from` to `to`. Depending on `force`, we might not actually create a
/// new copy or symlink if `to` matches `from_hash`.
fn install_file(
    force: bool,
    method: InstallationMethod,
    from: &Path,
    from_hash: Option<i64>,
    to: &Path,
) -> Result<bool> {
    // We'll only recreate existing files when updating yabridge, when switching between the symlink
    // and copy installation methods, or when the `force` option is set. If the target file already
    // exists and does not require updating, we'll just skip the file since some DAWs will otherwise
    // unnecessarily reindex the file. We check `std::fs::symlink_metadata` instead of
    // `Path::exists()` because the latter reports false for broken symlinks.
    if let Ok(metadata) = fs::symlink_metadata(&to) {
        match (force, &method) {
            (false, InstallationMethod::Copy) => {
                // If the target file is already a real file (not a symlink) and its hash is the
                // same as that of the `from` file we're trying to copy there, then we don't have to
                // do anything
                if let Some(hash) = from_hash {
                    if metadata.file_type().is_file() && utils::hash_file(to)? == hash {
                        return Ok(false);
                    }
                }
            }
            (false, InstallationMethod::Symlink) => {
                // If the target file is already a symlink to `from`, then we can skip this file
                if metadata.file_type().is_symlink() && to.read_link()? == from {
                    return Ok(false);
                }
            }
            // With the force option we always want to recreate existing .so files
            (true, _) => (),
        }

        utils::remove_file(&to)?;
    };

    match method {
        InstallationMethod::Copy => {
            utils::copy_or_reflink(from, to)?;
        }
        InstallationMethod::Symlink => {
            utils::symlink(from, to)?;
        }
    }

    Ok(true)
}
