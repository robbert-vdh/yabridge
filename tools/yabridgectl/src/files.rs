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

//! Functions to index plugins and to set up yabridge for those plugins.

use aho_corasick::AhoCorasick;
use anyhow::{Context, Result};
use lazy_static::lazy_static;
use rayon::prelude::*;
use std::collections::{BTreeMap, HashMap};
use std::fmt::Display;
use std::path::{Path, PathBuf};
use std::process::Command;
use walkdir::WalkDir;

use crate::config::yabridge_vst3_home;
use crate::utils::get_file_type;

/// Stores the results from searching through a directory. We'll search for Windows VST2 plugin
/// `.dll` files, Windows VST3 plugin modules, and native Linux `.so` files inside of a directory.
/// These `.so` files are kept track of so we can report the current installation status of VST2
/// plugins and to be able to prune orphan files. Since VST3 plugins have to be instaleld in
/// `~/.vst3`, these orphan files are only relevant for VST2 plugins.
#[derive(Debug)]
pub struct SearchResults {
    /// Absolute paths to the found VST2 `.dll` files.
    pub vst2_files: Vec<PathBuf>,
    /// Absolute paths to found VST3 modules. Either legacy `.vst3` DLL files or VST 3.6.10 bundles.
    pub vst3_modules: Vec<Vst3Module>,
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

/// VST3 modules we found during a serach.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Vst3Module {
    /// Old, pre-VST 3.6.10 style `.vst3` modules. These are simply `.dll` files with a different p
    /// refix. Even though this is a legacy format, almost all VST3 plugins in the wild still use
    /// this format.
    Legacy(PathBuf, LibArchitecture),
    /// A VST 3.6.10 bundle, with the same format as the VST3 bundles used on Linux and macOS. These
    /// kinds of bundles can come with resource files and presets, which should also be symlinked to
    /// `~/.vst3/`
    Bundle(PathBuf, LibArchitecture),
}

impl Vst3Module {
    /// The architecture of this VST3 module.
    pub fn architecture(&self) -> LibArchitecture {
        match &self {
            Vst3Module::Legacy(_, architecture) | Vst3Module::Bundle(_, architecture) => {
                *architecture
            }
        }
    }

    /// Get the path to the Windows VST3 plugin. This can be either a file or a directory depending
    /// on the type of moudle.
    pub fn original_path(&self) -> &Path {
        match &self {
            Vst3Module::Legacy(path, _) | Vst3Module::Bundle(path, _) => path,
        }
    }

    /// Get the name of the module as a string. Should be in the format `Plugin Name.vst3`.
    pub fn original_module_name(&self) -> &str {
        match &self {
            Vst3Module::Legacy(path, _) | Vst3Module::Bundle(path, _) => path
                .file_name()
                .unwrap()
                .to_str()
                .expect("VST3 module name contains invalid UTF-8"),
        }
    }

    /// Get the path to the actual `.vst3` module file.
    pub fn original_module_path(&self) -> PathBuf {
        match &self {
            Vst3Module::Legacy(path, _) => path.to_owned(),
            Vst3Module::Bundle(bundle_home, architecture) => {
                let mut path = bundle_home.join("Contents");
                path.push(architecture.vst_arch());
                path.push(self.original_module_name());

                path
            }
        }
    }

    /// Get the path to the bundle in `~/.vst3` corresponding to the bridged version of this module.
    ///
    /// FIXME: How do we solve naming clashes from the same VST3 plugin being installed to multiple
    ///        Wine prefixes?
    pub fn yabridge_bundle_home(&self) -> PathBuf {
        yabridge_vst3_home().join(self.original_module_name())
    }

    /// Get the path to the `libyabridge.so` file in `~/.vst3` corresponding to the bridged version
    /// of this module.
    pub fn yabridge_native_module_path(&self) -> PathBuf {
        let native_module_name = match &self {
            Vst3Module::Legacy(path, _) | Vst3Module::Bundle(path, _) => path
                .with_extension("so")
                .file_name()
                .unwrap()
                .to_str()
                .expect("VST3 module name contains invalid UTF-8")
                .to_owned(),
        };

        let mut path = self.yabridge_bundle_home();
        path.push("Contents");
        path.push("x86_64-linux");
        path.push(native_module_name);
        path
    }

    /// Get the path to where we'll symlink `original_module_path`. This is part of the merged VST3
    /// bundle in `~/.vst3/yabridge`.
    pub fn yabridge_windows_module_path(&self) -> PathBuf {
        let mut path = self.yabridge_bundle_home();
        path.push("Contents");
        path.push(self.architecture().vst_arch());
        path.push(self.original_module_name());
        path
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
    /// For every found VST2 plugin and VST3 module, find the associated copy or symlink of
    /// `libyabridge-{vst2,vst3}.so`. The returned hashmap will contain a `None` value for plugins
    /// that have not yet been set up.
    pub fn installation_status(&self) -> BTreeMap<PathBuf, Option<NativeFile>> {
        let so_files: HashMap<&Path, &NativeFile> = self
            .so_files
            .iter()
            .map(|file| (file.path(), file))
            .collect();

        // Do this for the VST2 plugins
        let mut installation_status: BTreeMap<PathBuf, Option<NativeFile>> = self
            .vst2_files
            .iter()
            .map(
                |path| match so_files.get(path.with_extension("so").as_path()) {
                    Some(&file_type) => (path.clone(), Some(file_type.clone())),
                    None => (path.clone(), None),
                },
            )
            .collect();

        // And for VST3 modules. We have not stored the paths to the corresponding `.so` files yet
        // because they are not in any of the directories we're indexing.
        installation_status.extend(self.vst3_modules.iter().map(|module| {
            let module_path = module.yabridge_native_module_path();
            let install_type = get_file_type(module_path.clone());
            (module_path, install_type)
        }));

        installation_status
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

        for vst2_path in &self.vst2_files {
            orphans.remove(vst2_path.with_extension("so").as_path());
        }

        orphans.values().cloned().collect()
    }
}

/// Find all `.dll`, `.vst3` and `.so` files under a directory. These results can be filtered down
/// to actual VST2 plugins and VST3 modules using `search()`.
pub fn index(directory: &Path) -> SearchIndex {
    let mut dll_files: Vec<PathBuf> = Vec::new();
    let mut vst3_files: Vec<PathBuf> = Vec::new();
    let mut so_files: Vec<NativeFile> = Vec::new();
    // XXX: We're silently skipping directories and files we don't have permission to read. This
    //      sounds like the expected behavior, but I"m not entirely sure.
    for (file_idx, entry) in WalkDir::new(directory)
        .follow_links(true)
        .into_iter()
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
        let is_vst2_plugin: Vec<Result<PathBuf, PathBuf>> = self
            .dll_files
            .into_par_iter()
            .map(|path| {
                if VST2_AUTOMATON.is_match(exported_functions(&path)?) {
                    Ok(Ok(path))
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

                    if module_is_in_bundle {
                        Ok(Ok(Vst3Module::Bundle(
                            bundle_root.unwrap().to_owned(),
                            architecture,
                        )))
                    } else {
                        Ok(Ok(Vst3Module::Legacy(module_path, architecture)))
                    }
                } else {
                    Ok(Err(module_path))
                }
            })
            .collect::<Result<_>>()?;

        let mut skipped_files: Vec<PathBuf> = Vec::new();

        let mut vst2_files: Vec<PathBuf> = Vec::new();
        for dandidate in is_vst2_plugin {
            match dandidate {
                Ok(path) => vst2_files.push(path),
                Err(path) => skipped_files.push(path),
            }
        }

        let mut vst3_modules: Vec<Vst3Module> = Vec::new();
        for candidate in is_vst3_module {
            match candidate {
                Ok(module) => vst3_modules.push(module),
                Err(path) => skipped_files.push(path),
            }
        }

        Ok(SearchResults {
            vst2_files,
            vst3_modules,
            skipped_files,
            so_files: self.so_files,
        })
    }
}
