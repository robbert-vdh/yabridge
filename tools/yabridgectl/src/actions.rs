// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2022 Robbert van der Helm
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

use crate::config::{
    yabridge_vst2_home, yabridge_vst3_home, Config, Vst2InstallationLocation, YabridgeFiles,
};
use crate::files::{self, NativeFile, Plugin, Vst2Plugin};
use crate::util::{self, get_file_type};
use crate::util::{verify_path_setup, verify_wine_setup};
use crate::vst3_moduleinfo::ModuleInfo;

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
                    util::remove_file(file.path())?;
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

    match config.vst2_location {
        Vst2InstallationLocation::Centralized => {
            println!("VST2 location: '{}'", yabridge_vst2_home().display());
        }
        Vst2InstallationLocation::Inline => {
            println!("VST2 location: inline next to the Windows plugin file");
        }
    }
    // This is fixed, but just from a UX point of view it might be nice to have as a reminder
    println!("VST3 location: '{}'\n", yabridge_vst3_home().display());

    let files = config.files();
    match &files {
        Ok(files) => {
            println!(
                "libyabridge-chainloader-vst2.so: '{}' ({})",
                files.vst2_chainloader.display(),
                files.vst2_chainloader_arch,
            );
            println!(
                "libyabridge-chainloader-vst3.so: {}\n",
                files
                    .vst3_chainloader
                    .as_ref()
                    .map(|(path, arch)| format!("'{}' ({})", path.display(), arch))
                    .unwrap_or_else(|| "<not found>".red().to_string())
            );
            println!(
                "yabridge-host.exe: {}",
                files
                    .yabridge_host_exe
                    .as_ref()
                    // We don't care about the actual path, but the file should at least exist
                    .zip(files.yabridge_host_exe_so.as_ref())
                    .map(|(path, _)| format!("'{}'", path.display()))
                    .unwrap_or_else(|| "<not found>".red().to_string())
            );
            println!(
                "yabridge-host-32.exe: {}",
                files
                    .yabridge_host_32_exe
                    .as_ref()
                    .zip(files.yabridge_host_32_exe_so.as_ref())
                    .map(|(path, _)| format!("'{}'", path.display()))
                    .unwrap_or_else(|| "<not found>".red().to_string())
            );
        }
        Err(err) => {
            println!("Could not find yabridge's files: {}", err);
        }
    }

    for (path, search_results) in results {
        // Always print these paths with trailing slashes for consistency's sake because paths can
        // be added both with and without a trailing slash
        println!("\n{}", path.join("").display());

        for (plugin_path, (plugin, status)) in
            search_results.installation_status(config, files.as_ref().ok())
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

            // This made more sense when we supported symlinking `libyabridge-*.so`, but we should
            // display _something_ to indicate that the plugin is set up correctly
            let status_str = match status {
                Some(NativeFile::Regular(_)) => "synced".green(),
                // This should not occur, but we'll display it just in case it does happen
                Some(NativeFile::Symlink(_)) => "symlink".yellow(),
                Some(NativeFile::Directory(_)) => "invalid".red(),
                None => "not yet synced".into(),
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
    pub path: Option<PathBuf>,
    pub path_auto: bool,
    pub vst2_location: Option<&'a str>,
    pub no_verify: Option<bool>,
}

/// Change configuration settings. The actual options are defined in the clap [app](clap::App).
pub fn set_settings(config: &mut Config, options: &SetOptions) -> Result<()> {
    if let Some(path) = &options.path {
        config.yabridge_home = Some(path.clone());
    }

    if options.path_auto {
        config.yabridge_home = None;
    }

    match options.vst2_location {
        Some("centralized") => config.vst2_location = Vst2InstallationLocation::Centralized,
        Some("inline") => config.vst2_location = Vst2InstallationLocation::Inline,
        Some(s) => unimplemented!("Unexpected installation method '{}'", s),
        None => (),
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
    let vst2_chainloader_hash = util::hash_file(&files.vst2_chainloader)?;
    let vst3_chainloader_hash = match &files.vst3_chainloader {
        Some((path, _)) => Some(util::hash_file(path)?),
        None => None,
    };

    if let Some((vst3_chainloader_path, _)) = &files.vst3_chainloader {
        println!("Setting up VST2 and VST3 plugins using:");
        println!("- {}", files.vst2_chainloader.display());
        println!("- {}\n", vst3_chainloader_path.display());
    } else {
        println!("Setting up VST2 plugins using:");
        println!("- {}\n", files.vst2_chainloader.display());
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
    // Paths added to this and to the `new_plugins` set below should be normalized with
    // `utils::normalize_path()` so that the reported numbers are still correct when encountering
    // overlapping symlinked paths.
    let mut managed_plugins: HashSet<PathBuf> = HashSet::new();
    // The plugins we created a new copy of `libyabridge-chainloader-{vst2,vst3}.so` for. We don't
    // touch these files if they're already up to date to prevent hosts from unnecessarily
    // rescanning the plugins.
    let mut new_plugins: HashSet<PathBuf> = HashSet::new();
    // The files we skipped during the scan because they turned out to not be plugins
    let mut skipped_dll_files: Vec<PathBuf> = Vec::new();
    // `.so` files and unused VST3 modules we found during scanning that didn't have a corresponding
    // copy or symlink of `libyabridge-chainloader-vst2.so`
    let mut orphan_files: Vec<NativeFile> = Vec::new();
    // When using the centralized VST2 installation location in `~/.vst/yabridge` we'll want to
    // track all unmanaged files in that directory and add them to the orphans list
    let mut known_centralized_vst2_files: HashSet<PathBuf> = HashSet::new();
    // Since VST3 bundles contain multiple files from multiple sources (native library files from
    // yabridge, and symlinks to Windows VST3 modules or bundles), cleaning up orphan VST3 files is
    // a bit more complicated. We want to clean both `.vst3` bundles that weren't used by anything
    // during the syncing process, so we'll keep track of which VST3 files we touched per-bundle. We
    // can then at the end remove all unkonwn bundles, and all unkonwn files within a bundle.
    let mut known_centralized_vst3_files: HashMap<PathBuf, HashSet<PathBuf>> = HashMap::new();
    for (path, search_results) in results {
        // Orphan files in the centralized directories need to be detected separately
        orphan_files.extend(
            search_results
                .vst2_inline_orphans(config)
                .into_iter()
                .cloned(),
        );
        skipped_dll_files.extend(search_results.skipped_files);

        if options.verbose {
            // Always print these paths with trailing slashes for consistency's sake because paths
            // can be added both with and without a trailing slash
            println!("{}", path.join("").display());
        }

        for plugin in search_results.plugins {
            // If verbose mode is enabled we'll print the path to the plugin after setting it up
            let plugin_path: PathBuf = match plugin {
                // VST2 plugins can be set up in either `~/.vst/yabridge` or inline with the
                // plugin's `.dll` file
                Plugin::Vst2(vst2_plugin) => {
                    match config.vst2_location {
                        Vst2InstallationLocation::Centralized => {
                            let target_native_plugin_path = vst2_plugin.centralized_native_target();
                            let target_windows_plugin_path =
                                vst2_plugin.centralized_windows_target();
                            let normalized_target_native_plugin_path =
                                util::normalize_path(&target_native_plugin_path);

                            let mut is_new = known_centralized_vst2_files
                                .insert(target_native_plugin_path.clone());
                            is_new |= known_centralized_vst2_files
                                .insert(target_windows_plugin_path.clone());
                            if !is_new {
                                eprintln!(
                                    "{}",
                                    util::wrap(&format!(
                                        "{}: '{}' has already been provided by another Wine prefix or plugin directory, skipping it\n",
                                        "WARNING".red(),
                                        target_windows_plugin_path.display(),
                                    ))
                                );

                                continue;
                            }

                            // In the centralized mode we'll create a copy of
                            // `libyabridge-chainloader-vst2.so` to (a subdirectory of)
                            // `~/.vst/yabridge`, and then we'll symlink the Windows VST2 plugin
                            // `.dll` file right next to it
                            util::create_dir_all(target_native_plugin_path.parent().unwrap())?;
                            if install_file(
                                options.force,
                                InstallationMethod::Copy,
                                &files.vst2_chainloader,
                                Some(vst2_chainloader_hash),
                                &target_native_plugin_path,
                            )? {
                                new_plugins.insert(normalized_target_native_plugin_path.clone());
                            }
                            managed_plugins.insert(normalized_target_native_plugin_path);

                            install_file(
                                true,
                                InstallationMethod::Symlink,
                                &vst2_plugin.path,
                                None,
                                &target_windows_plugin_path,
                            )?;
                        }
                        Vst2InstallationLocation::Inline => {
                            let target_path = vst2_plugin.inline_native_target();
                            let normalized_target_path = util::normalize_path(&target_path);

                            // Since we skip some files, we'll also keep track of how many new file we've
                            // actually set up
                            if install_file(
                                options.force,
                                InstallationMethod::Copy,
                                &files.vst2_chainloader,
                                Some(vst2_chainloader_hash),
                                &target_path,
                            )? {
                                new_plugins.insert(normalized_target_path.clone());
                            }
                            managed_plugins.insert(normalized_target_path);
                        }
                    }

                    vst2_plugin.path.clone()
                }
                // And then create merged bundles for the VST3 plugins:
                // https://developer.steinberg.help/display/VST/Plug-in+Format+Structure#PluginFormatStructure-MergedBundle
                Plugin::Vst3(module) => {
                    // Only set up VST3 plugins when yabridge has been compiled with VST3 support
                    if vst3_chainloader_hash.is_none() {
                        continue;
                    }

                    let target_bundle_home = module.target_bundle_home();
                    let target_native_module_path = module.target_native_module_path(Some(&files));
                    let target_windows_module_path = module.target_windows_module_path();
                    let normalized_native_module_path =
                        util::normalize_path(&target_native_module_path);

                    // 32-bit and 64-bit versions of the plugin can live inside of the same bundle),
                    // but it's not possible to use the exact same plugin from multiple Wine
                    // prefixes at the same time so we'll warn when that happens
                    let managed_vst3_bundle_files = known_centralized_vst3_files
                        .entry(target_bundle_home.clone())
                        .or_insert_with(HashSet::new);
                    if managed_vst3_bundle_files.contains(&target_windows_module_path) {
                        eprintln!(
                            "{}",
                            util::wrap(&format!(
                                "{}: The {} version of '{}' has already been provided by another Wine \
                                prefix or plugin directory, skipping '{}'\n",
                                "WARNING".red(),
                                module.architecture,
                                module.target_bundle_home().display(),
                                module.original_module_path().display(),
                            ))
                        );

                        continue;
                    }

                    // We're building a merged VST3 bundle containing both a copy or symlink to
                    // `libyabridge-chainloader-vst3.so` and the Windows VST3 plugin. The path to
                    // this native module will depend on whether `libyabridge-chainloader-vst3.so`
                    // is a 32-bit or a 64-bit library file.
                    util::create_dir_all(target_native_module_path.parent().unwrap())?;
                    if install_file(
                        options.force,
                        InstallationMethod::Copy,
                        &files.vst3_chainloader.as_ref().unwrap().0,
                        vst3_chainloader_hash,
                        &target_native_module_path,
                    )? {
                        // We're counting the native `.so` files and not the Windows VST3 plugins
                        // because even though the 32-bit and 64-bit versions of a plugin are
                        // technically separate plugins, we can only use one at a time anyways
                        // because of how these bundles work
                        new_plugins.insert(normalized_native_module_path.clone());
                    }
                    managed_plugins.insert(normalized_native_module_path.clone());
                    managed_vst3_bundle_files.insert(target_native_module_path);

                    // We'll then symlink the Windows VST3 module to that bundle to create a merged
                    // bundle: https://developer.steinberg.help/display/VST/Plug-in+Format+Structure#PluginFormatStructure-MergedBundle
                    util::create_dir_all(target_windows_module_path.parent().unwrap())?;
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
                    // TODO: Also handle `moduleinfo.json` files. That would require translating the
                    //       UIDs from the COM-format to the non-COM format. Yabridge currently does
                    //       not suport this because supporting the accompanying
                    //       `IPluginCompatibility` would require having to add a JSON parser to
                    //       yabridge just for that.
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

                    // If the plugin has a VST 3.7.10 moduleinfo file, then we'll rewrite the byte
                    // orders of the class IDs stored within the file and then write it to the
                    // bridged VST3 bundle.
                    // https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/VST+Module+Architecture/ModuleInfo-JSON.html
                    if let Some(original_moduleinfo_path) = module.original_moduleinfo_path() {
                        let target_moduleinfo_path = module.target_moduleinfo_path();

                        let result = util::read_to_string(&original_moduleinfo_path)
                            .and_then(|module_info_json| {
                                serde_jsonrc::from_str(&module_info_json)
                                    .context("Could not parse JSON file")
                            })
                            .and_then(|mut module_info: ModuleInfo| {
                                module_info.rewrite_uid_byte_orders()?;
                                Ok(module_info)
                            })
                            .and_then(|converted_module_info| {
                                let converted_json =
                                    serde_jsonrc::to_string_pretty(&converted_module_info)
                                        .context("Could not format JSON file")?;
                                util::write(target_moduleinfo_path, converted_json)
                            });
                        if let Err(error) = result {
                            eprintln!(
                                "Error converting '{}', skipping...\n{}",
                                original_moduleinfo_path.display(),
                                error
                            );
                        }
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

    // We've already kept track of orphan `.dll` files in the plugin directories, but now we need to
    // do something similar for orphan files in `~/.vst/yabridge` and `~/.vst3/yabridge`. For VST3
    // plugins we'll want to remove both unmanaged VST3 bundles in `~/.vst3/yabridge` as well as
    // unmanged files within managed bundles. That's why we'll immediately filter out known files
    // within VST3 bundles. For VST2 plugins we can simply treat any file in `~/.vst/yabridge` that
    // we did not add to `known_centralized_vst2_files` as an orphan. We'll want to do this
    // regardless of the VST2 installation location setting so switching between the two modes and
    // then pruning works as expected.
    // TODO: Move this elsewhere
    let centralized_vst2_files = WalkDir::new(yabridge_vst2_home())
        .follow_links(true)
        .same_file_system(true)
        .into_iter()
        .filter_map(|e| {
            let path = match e {
                Ok(entry) => entry.path().to_owned(),
                Err(err) => err.path()?.to_owned(),
            };

            if !path.is_dir() && matches!(path.extension()?.to_str()?, "dll" | "so") {
                Some(path)
            } else {
                None
            }
        });
    let installed_vst3_bundles = WalkDir::new(yabridge_vst3_home())
        .follow_links(true)
        .same_file_system(true)
        .into_iter()
        .filter_entry(|entry| entry.file_type().is_dir())
        .filter_map(|e| {
            let path = match e {
                Ok(entry) => entry.path().to_owned(),
                Err(err) => err.path()?.to_owned(),
            };

            if path.extension()?.to_str()? == "vst3" {
                Some(path)
            } else {
                None
            }
        });

    orphan_files.extend(centralized_vst2_files.filter_map(|path| {
        if known_centralized_vst2_files.contains(&path) {
            None
        } else {
            get_file_type(path)
        }
    }));
    for bundle_path in installed_vst3_bundles {
        match known_centralized_vst3_files.get(&bundle_path) {
            None => orphan_files.push(NativeFile::Directory(bundle_path)),
            Some(managed_vst3_bundle_files) => {
                // Find orphan files and symlinks within this bundle. We need this to be able to
                // switch between 32-bit and 64-bit versions of both yabridge and the Windows plugin
                orphan_files.extend(
                    WalkDir::new(bundle_path)
                        .follow_links(false)
                        .into_iter()
                        .filter_map(|e| {
                            let path = match e {
                                Ok(entry) => entry.path().to_owned(),
                                Err(err) => err.path()?.to_owned(),
                            };

                            let managed_file = managed_vst3_bundle_files.contains(&path);
                            match get_file_type(path).unwrap() {
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

        // NOTE: This is done in reverse lexicographical order to make sure subdirectories are
        //       cleaned before their parent directories
        orphan_files.sort_by(|a, b| b.path().cmp(a.path()));
        for file in orphan_files.into_iter() {
            println!("- {}", file.path().display());
            if options.prune {
                match &file {
                    NativeFile::Regular(path) | NativeFile::Symlink(path) => {
                        util::remove_file(path)?;
                    }
                    NativeFile::Directory(path) => {
                        util::remove_dir_all(path)?;
                    }
                }

                // If the directory `file` was in is now empty, then we'll also recursively prune
                // the empty subdirectory
                let mut parent_dir = file.path().parent();
                while let Some(dir) =
                    parent_dir.and_then(|dir| fs::remove_dir(dir).ok().map(|_| dir))
                {
                    parent_dir = dir.parent();
                }
            }
        }

        println!();
    }

    // Don't mind the ugly format string, the existence of the symlink-based installation method
    // should be hidden as much as possible until it gets removed in yabridge 4.0
    println!(
        "Finished setting up {} plugins ({} new), skipped {} non-plugin .dll files",
        managed_plugins.len(),
        new_plugins.len(),
        num_skipped_files
    );

    // Skipping the post-installation seting checks can be done only for this invocation of
    // `yabridgectl sync`, or it can be skipped permanently through a config file option
    if options.no_verify || config.no_verify {
        return Ok(());
    }

    // The path setup is to make sure that the `libyabridge-chainloader-{vst2,vst3}.so` copies can
    // find `yabridge-host.exe` and by extension the plugin libraries. That last part should already
    // be the case if we get to this point though.
    verify_path_setup(config)?;

    // This check is only performed once per combination of Wine and yabridge versions
    verify_wine_setup(config)?;

    Ok(())
}

// TODO: Clean this up, in the past this was part of a yabridgectl setting and the enum was simply
//       reused here
enum InstallationMethod {
    Copy,
    Symlink,
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
                    if metadata.file_type().is_file() && util::hash_file(to)? == hash {
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

        util::remove_file(&to)?;
    };

    match method {
        InstallationMethod::Copy => {
            util::copy_or_reflink(from, to)?;
        }
        InstallationMethod::Symlink => {
            util::symlink(from, to)?;
        }
    }

    Ok(true)
}
