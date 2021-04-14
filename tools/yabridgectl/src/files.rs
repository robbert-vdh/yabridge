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

//! Functions to index plugins and to set up yabridge for those plugins.

use aho_corasick::AhoCorasick;
use anyhow::{Context, Result};
use lazy_static::lazy_static;
use rayon::prelude::*;
use std::collections::{BTreeMap, HashMap, HashSet};
use std::fmt::Display;
use std::path::{Path, PathBuf};
use std::process::Command;
use walkdir::WalkDir;

use crate::config::yabridge_vst3_home;
use crate::utils::get_file_type;

/// Stores the results from searching through a directory. We'll search for Windows VST2 plugin
/// `.dll` files, Windows VST3 plugin modules, and native Linux `.so` files inside of a directory.
/// These `.so` files are kept track of so we can report the current installation status of VST2
/// plugins and to be able to prune orphan files. Since VST3 plugins have to be installed in
/// `~/.vst3`, these orphan files are only relevant for VST2 plugins.
#[derive(Debug)]
pub struct SearchResults {
    /// The plugins found during the search. This contains both VST2 plugins and VST3 modules.
    pub plugins: Vec<Plugin>,
    /// `.dll` files skipped over during the search. Used for printing statistics and shown when
    /// running `yabridgectl sync --verbose`.
    pub skipped_files: Vec<PathBuf>,

    /// Absolute paths to any `.so` files inside of the directory, and whether they're a symlink or
    /// a regular file.
    pub so_files: Vec<NativeFile>,
}

/// The results of the first step of the search process. We'll first index all possibly relevant
/// files in a directory before filtering them down to a `SearchResults` object.
#[derive(Debug)]
pub struct SearchIndex {
    /// Any `.dll` file.
    pub dll_files: Vec<PathBuf>,
    /// Any `.vst3` file or directory. This can be either a legacy `.vst3` DLL module or a VST
    /// 3.6.10 module (or some kind of random other file, of course).
    pub vst3_files: Vec<PathBuf>,
    /// Absolute paths to any `.so` files inside of the directory, and whether they're a symlink or
    /// a regular file.
    pub so_files: Vec<NativeFile>,
}

/// Native `.so` files and VST3 bundle directories we found during a search.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum NativeFile {
    Symlink(PathBuf),
    Regular(PathBuf),
    Directory(PathBuf),
}

impl NativeFile {
    /// Return the path of a found `.so` file.
    pub fn path(&self) -> &Path {
        match &self {
            NativeFile::Symlink(path) | NativeFile::Regular(path) | NativeFile::Directory(path) => {
                path
            }
        }
    }
}

/// A plugin as found during the search. This can be either a VST2 plugin or a VST3 module.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Plugin {
    Vst2(Vst2Plugin),
    Vst3(Vst3Module),
}

/// VST2 plugins we found during a search along with their architecture.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Vst2Plugin {
    /// The absolute path to the VST2 plugin `.dll` file.
    pub path: PathBuf,
    /// The architecture of the VST2 plugin.
    pub architecture: LibArchitecture,
}

/// VST3 modules we found during a search.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Vst3Module {
    /// The absolute path to the actual VST3 module and its type.
    pub module: Vst3ModuleType,
    /// The architecture of the VST3 module.
    pub architecture: LibArchitecture,
    /// The VST3 subdirectory the orignal module was in, if any. This is usually used to group
    /// plugisn by the same manufacturer together. We detect this by looking for a parent `VST3`
    /// directory. If we can't find that, this will be `None`.
    pub subdirectory: Option<PathBuf>,
}

/// The type of the VST3 module. VST 3.6.10 style bundles require slightly different handling
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Vst3ModuleType {
    /// Old, pre-VST 3.6.10 style `.vst3` modules. These are simply `.dll` files with a different p
    /// refix. Even though this is a legacy format, almost all VST3 plugins in the wild still use
    /// this format.
    Legacy(PathBuf),
    /// A VST 3.6.10 bundle, with the same format as the VST3 bundles used on Linux and macOS. These
    /// kinds of bundles can come with resource files and presets, which should also be symlinked to
    /// `~/.vst3/`
    Bundle(PathBuf),
}

impl Vst3Module {
    /// Get the path to the Windows VST3 plugin. This can be either a file or a directory depending
    /// on the type of moudle.
    pub fn original_path(&self) -> &Path {
        match &self.module {
            Vst3ModuleType::Legacy(path) | Vst3ModuleType::Bundle(path) => path,
        }
    }

    /// Get the name of the module as a string. Should be in the format `Plugin Name.vst3`.
    pub fn original_module_name(&self) -> &str {
        match &self.module {
            Vst3ModuleType::Legacy(path) | Vst3ModuleType::Bundle(path) => path
                .file_name()
                .unwrap()
                .to_str()
                .expect("VST3 module name contains invalid UTF-8"),
        }
    }

    /// Get the path to the actual `.vst3` module file.
    pub fn original_module_path(&self) -> PathBuf {
        match &self.module {
            Vst3ModuleType::Legacy(path) => path.to_owned(),
            Vst3ModuleType::Bundle(bundle_home) => {
                let mut path = bundle_home.join("Contents");
                path.push(self.architecture.vst_arch());
                path.push(self.original_module_name());

                path
            }
        }
    }

    /// If this was a VST 3.6.10 style bundle, then return the path to the `Resources` directory if
    /// it has one.
    pub fn original_resources_dir(&self) -> Option<PathBuf> {
        match &self.module {
            Vst3ModuleType::Bundle(bundle_home) => {
                let mut path = bundle_home.join("Contents");
                path.push("Resources");
                if path.exists() {
                    Some(path)
                } else {
                    None
                }
            }
            Vst3ModuleType::Legacy(_) => None,
        }
    }

    /// Get the path to the bundle in `~/.vst3` corresponding to the bridged version of this module.
    /// We will try to recreate the original subdirectory structure so plugins are still grouped by
    /// manufacturer.
    ///
    /// FIXME: How do we solve naming clashes from the same VST3 plugin being installed to multiple
    ///        Wine prefixes?
    pub fn target_bundle_home(&self) -> PathBuf {
        match &self.subdirectory {
            Some(directory) => yabridge_vst3_home()
                .join(directory)
                .join(self.original_module_name()),
            None => yabridge_vst3_home().join(self.original_module_name()),
        }
    }

    /// Get the path to the `libyabridge.so` file in `~/.vst3` corresponding to the bridged version
    /// of this module.
    pub fn target_native_module_path(&self) -> PathBuf {
        let native_module_name = match &self.module {
            Vst3ModuleType::Legacy(path) | Vst3ModuleType::Bundle(path) => path
                .with_extension("so")
                .file_name()
                .unwrap()
                .to_str()
                .expect("VST3 module name contains invalid UTF-8")
                .to_owned(),
        };

        let mut path = self.target_bundle_home();
        path.push("Contents");
        path.push("x86_64-linux");
        path.push(native_module_name);
        path
    }

    /// Get the path to where we'll symlink `original_module_path`. This is part of the merged VST3
    /// bundle in `~/.vst3/yabridge`.
    pub fn target_windows_module_path(&self) -> PathBuf {
        let mut path = self.target_bundle_home();
        path.push("Contents");
        path.push(self.architecture.vst_arch());
        path.push(self.original_module_name());
        path
    }

    /// If the Windows VST3 plugin we're bridging was in a VST 3.6.10 style bundle and had a
    /// resources directory, then we'll symlink that directory to here so the host can access all
    /// its original resources.
    pub fn target_resources_dir(&self) -> PathBuf {
        let mut path = self.target_bundle_home();
        path.push("Contents");
        path.push("Resources");
        path
    }

    /// Get a textual representation of the module type. Used in `yabridgectl status`.
    pub fn type_str(&self) -> &str {
        match &self.module {
            Vst3ModuleType::Legacy(_) => "legacy",
            Vst3ModuleType::Bundle(_) => "bundle",
        }
    }
}

/// The architecture of a `.dll` file. Needed so we can create a merged bundle for VST3 plugins.
#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Copy)]
pub enum LibArchitecture {
    Dll32,
    Dll64,
}

impl Display for LibArchitecture {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match &self {
            LibArchitecture::Dll32 => write!(f, "32-bit"),
            LibArchitecture::Dll64 => write!(f, "64-bit"),
        }
    }
}

impl LibArchitecture {
    /// Get the corresponding VST3 architecture directory name. See
    /// https://steinbergmedia.github.io/vst3_doc/vstinterfaces/vst3loc.html.
    pub fn vst_arch(&self) -> &str {
        match &self {
            LibArchitecture::Dll32 => "x86-win",
            LibArchitecture::Dll64 => "x86_64-win",
        }
    }
}

impl SearchResults {
    /// Create a map out of all found plugins based on their file path that contains both a
    /// reference to the plugin (so we can print information about it) and the current installation
    /// status. The installation status will be `None` if the plugin has not yet been set up.
    pub fn installation_status(&self) -> BTreeMap<PathBuf, (&Plugin, Option<NativeFile>)> {
        let so_files: HashMap<&Path, &NativeFile> = self
            .so_files
            .iter()
            .map(|file| (file.path(), file))
            .collect();

        self.plugins
            .iter()
            .map(|plugin| match plugin {
                Plugin::Vst2(Vst2Plugin { path, .. }) => {
                    // For VST2 plugins we'll just look at the similarly named `.so` file right next
                    // to the plugin `.dll` file.
                    match so_files.get(path.with_extension("so").as_path()) {
                        Some(&file_type) => (path.clone(), (plugin, Some(file_type.clone()))),
                        None => (path.clone(), (plugin, None)),
                    }
                }
                // We have not stored the paths to the corresponding `.so` files yet for VST3
                // modules because they are not in any of the directories we're indexing
                Plugin::Vst3(vst3_module) => (
                    vst3_module.original_path().to_owned(),
                    (
                        plugin,
                        get_file_type(vst3_module.target_native_module_path()),
                    ),
                ),
            })
            .collect()
    }

    /// Find all `.so` files in the search results that do not belong to a VST2 plugin `.dll` file.
    /// We cannot yet do the same thing for VST3 plguins because they will all be installed in
    /// `~/.vst3`.
    pub fn vst2_orphans(&self) -> Vec<&NativeFile> {
        // We need to store these in a map so we can easily entries with corresponding `.dll` files
        let mut orphans: HashMap<&Path, &NativeFile> = self
            .so_files
            .iter()
            .map(|file_type| (file_type.path(), file_type))
            .collect();

        for plugin in &self.plugins {
            if let Plugin::Vst2(Vst2Plugin { path, .. }) = plugin {
                orphans.remove(path.with_extension("so").as_path());
            }
        }

        orphans.values().cloned().collect()
    }
}

/// Find all `.dll`, `.vst3` and `.so` files under a directory. These results can be filtered down
/// to actual VST2 plugins and VST3 modules using `search()`. Any path found in the blacklist will
/// be pruned immediately, so this can be used to both not index individual files and to skip an
/// entire directory.
pub fn index(directory: &Path, blacklist: &HashSet<&Path>) -> SearchIndex {
    let mut dll_files: Vec<PathBuf> = Vec::new();
    let mut vst3_files: Vec<PathBuf> = Vec::new();
    let mut so_files: Vec<NativeFile> = Vec::new();
    // XXX: We're silently skipping directories and files we don't have permission to read. This
    //      sounds like the expected behavior, but I"m not entirely sure.
    for (file_idx, entry) in WalkDir::new(directory)
        .follow_links(true)
        .into_iter()
        .filter_entry(|e| !blacklist.contains(e.path()))
        .filter_map(|e| e.ok())
        .filter(|e| !e.file_type().is_dir())
        .enumerate()
    {
        // This is a bit of an odd warning, but I can see it happening that someone adds their
        // entire home directory by accident. Removing the home directory would cause yabridgectl to
        // scan for leftover `.so` files, which would of course take an enternity. This warning will
        // at least tell the user what's happening and that they can safely cancel the scan.
        if file_idx == 100_000 {
            eprintln!(
                "Indexed over 100.000 files, press Ctrl+C to cancel this operation if this was not \
                 intentional."
            )
        }

        match entry.path().extension().and_then(|os| os.to_str()) {
            Some("dll") => dll_files.push(entry.into_path()),
            Some("vst3") => vst3_files.push(entry.into_path()),
            Some("so") => {
                if entry.path_is_symlink() {
                    so_files.push(NativeFile::Symlink(entry.into_path()));
                } else {
                    so_files.push(NativeFile::Regular(entry.into_path()));
                }
            }
            _ => (),
        }
    }

    SearchIndex {
        dll_files,
        vst3_files,
        so_files,
    }
}

impl SearchIndex {
    /// Filter these indexing results down to actual VST2 plugins and VST3 modules. This will skip
    /// all invalid files, such as regular `.dll` libraries. Will return an error if `winedump`
    /// could not be found.
    pub fn search(self) -> Result<SearchResults> {
        lazy_static! {
            static ref VST2_AUTOMATON: AhoCorasick =
                AhoCorasick::new_auto_configured(&["VSTPluginMain", "main", "main_plugin"]);
            static ref VST3_AUTOMATON: AhoCorasick =
                AhoCorasick::new_auto_configured(&["GetPluginFactory"]);
            static ref DLL32_AUTOMATON: AhoCorasick =
                AhoCorasick::new_auto_configured(&["Machine:                      014C"]);
        }

        let winedump = |args: &[&str], path: &Path| {
            Command::new("winedump")
                .args(args)
                .arg(path)
                .output()
                .context(
                    "Could not find 'winedump'. In some distributions this is part of a seperate \
                     Wine tools package.",
                )
                .map(|output| output.stdout)
        };
        let pe32_info = |path: &Path| winedump(&[], path);
        let exported_functions = |path: &Path| winedump(&["-j", "export"], path);

        // We'll have to figure out which `.dll` files are VST2 plugins and which should be skipped
        // by checking whether the file contains one of the VST2 entry point functions. This vector
        // will contain an `Err(path)` if `path` was not a valid VST2 plugin.
        let is_vst2_plugin: Vec<Result<Vst2Plugin, PathBuf>> = self
            .dll_files
            .into_par_iter()
            .map(|path| {
                let architecture = if DLL32_AUTOMATON.is_match(pe32_info(&path)?) {
                    LibArchitecture::Dll32
                } else {
                    LibArchitecture::Dll64
                };

                if VST2_AUTOMATON.is_match(exported_functions(&path)?) {
                    Ok(Ok(Vst2Plugin { path, architecture }))
                } else {
                    Ok(Err(path))
                }
            })
            .collect::<Result<_>>()?;

        // We need to do the same thing with VST3 plugins. The added difficulty here is that we have
        // to figure out of the `.vst3` file is a legacy standalone VST3 module, or part of a VST
        // 3.6.10 bundle. We also need to know the plugin's architecture because we're going to
        // create a univeral VST3 bundle.
        let is_vst3_module: Vec<Result<Vst3Module, PathBuf>> = self
            .vst3_files
            .into_par_iter()
            .map(|module_path| {
                let architecture = if DLL32_AUTOMATON.is_match(pe32_info(&module_path)?) {
                    LibArchitecture::Dll32
                } else {
                    LibArchitecture::Dll64
                };

                if VST3_AUTOMATON.is_match(exported_functions(&module_path)?) {
                    // Now we'll have to figure out if the plugin is part of a VST 3.6.10 style
                    // bundle or a legacy `.vst3` DLL file. A WIndows VST3 bundle contains at least
                    // `<plugin_name>.vst3/Contents/<architecture_string>/<plugin_name>.vst3`, so
                    // we'll just go up a few directories and then reconstruct that bundle.
                    let module_name = module_path.file_name();
                    let bundle_root = module_path
                        .parent()
                        .and_then(|arch_dir| arch_dir.parent())
                        .and_then(|contents_dir| contents_dir.parent());
                    let module_is_in_bundle = bundle_root
                        .and_then(|bundle_root| bundle_root.parent())
                        .zip(module_name)
                        .map(|(path, module_name)| {
                            // Now reconstruct the path to the original file again as if it were in
                            // a bundle
                            let mut reconstructed_path = path.join(module_name);
                            reconstructed_path.push("Contents");
                            reconstructed_path.push(architecture.vst_arch());
                            reconstructed_path.push(module_name);

                            reconstructed_path.exists()
                        })
                        .unwrap_or(false);

                    let (module, module_home) = if module_is_in_bundle {
                        (
                            Vst3ModuleType::Bundle(bundle_root.unwrap().to_owned()),
                            bundle_root.unwrap(),
                        )
                    } else {
                        (
                            Vst3ModuleType::Legacy(module_path.clone()),
                            module_path.as_path(),
                        )
                    };

                    // We want to recreate the original subdirectory structure, so plugins are still
                    // grouped by manufacturer
                    let vst3_directory = module_home.ancestors().find(|path| {
                        path.file_name()
                            .and_then(|name| name.to_str())
                            .map(|name| name.to_lowercase().as_str() == "vst3")
                            .unwrap_or(false)
                    });
                    let subdirectory = match vst3_directory {
                        Some(directory) => module_home
                            .strip_prefix(directory)
                            .ok()
                            // We should of coruse pop the `.vst3` directory
                            .and_then(|suffix| suffix.parent())
                            .map(|subdirectory| subdirectory.to_owned()),
                        None => None,
                    };

                    Ok(Ok(Vst3Module {
                        module,
                        architecture,
                        subdirectory,
                    }))
                } else {
                    Ok(Err(module_path))
                }
            })
            .collect::<Result<_>>()?;

        let mut plugins: Vec<Plugin> = Vec::new();
        let mut skipped_files: Vec<PathBuf> = Vec::new();

        for dandidate in is_vst2_plugin {
            match dandidate {
                Ok(plugin) => plugins.push(Plugin::Vst2(plugin)),
                Err(path) => skipped_files.push(path),
            }
        }

        for candidate in is_vst3_module {
            match candidate {
                Ok(module) => plugins.push(Plugin::Vst3(module)),
                Err(path) => skipped_files.push(path),
            }
        }

        Ok(SearchResults {
            plugins,
            skipped_files,
            so_files: self.so_files,
        })
    }
}
