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

//! Functions to index plugins and to set up yabridge for those plugins.

use anyhow::Result;
use rayon::prelude::*;
use std::collections::{BTreeMap, HashMap, HashSet};
use std::fmt::Display;
use std::path::{Path, PathBuf};
use walkdir::WalkDir;

use crate::config::{
    yabridge_clap_home, yabridge_vst2_home, yabridge_vst3_home, Config, YabridgeFiles,
};
use crate::symbols::parse_pe32_binary;
use crate::util::get_file_type;

/// Stores the results from searching through a directory. We'll search for Windows VST2 plugin
/// `.dll` files, Windows VST3 plugin modules, and native Linux `.so` files inside of a directory.
/// These `.so` files are kept track of so we can report the current installation status of VST2
/// plugins and to be able to prune orphan files. Since yabridgectl 4.0 now sets up plugins in the
/// user's home directory, these inline orphans are mostly useful for cleaning up old installations
/// and when the user has explicitly enabled the inline installation location for VST2 plugins.
#[derive(Debug)]
pub struct SearchResults {
    /// The plugins found during the search. This contains VST2 plugins, VST3 modules, and CLAP
    /// plugins.
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
    /// Any `.dll` file, along with its relative path in the search directory.
    pub dll_files: Vec<(PathBuf, Option<PathBuf>)>,
    /// Any `.vst3` file or directory, along with its relative path in the search directory. This
    /// can be either a legacy `.vst3` DLL module or a VST 3.6.10 module (or some kind of random
    /// other file, of course).
    pub vst3_files: Vec<(PathBuf, Option<PathBuf>)>,
    /// Any `.clap` file, along with its relative path in the search directory.
    pub clap_files: Vec<(PathBuf, Option<PathBuf>)>,
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
    Clap(ClapPlugin),
}

/// VST2 plugins we found during a search along with their architecture.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Vst2Plugin {
    /// The absolute path to the VST2 plugin's `.dll` file.
    pub path: PathBuf,
    /// The architecture of the VST2 plugin.
    pub architecture: LibArchitecture,
    /// The subdirectory within the plugins directory the orignal plugin was in. If this could not
    /// be detected then this will be `None`.
    pub subdirectory: Option<PathBuf>,
}

/// VST3 modules we found during a search.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Vst3Module {
    /// The absolute path to the actual VST3 module and its type.
    pub module: Vst3ModuleType,
    /// The architecture of the VST3 module.
    pub architecture: LibArchitecture,
    /// The subdirectory within the plugins directory the orignal module was in. If this could not
    /// be detected then this will be `None`.
    pub subdirectory: Option<PathBuf>,
}

/// CLAP plugins we found during a search along with their architecture.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ClapPlugin {
    /// The absolute path to the Windows CLAP plugin's `.clap` file.
    pub path: PathBuf,
    /// The architecture of the CLAP plugin. This is supposed to be only the native architecture (no
    /// official x86 support), but we'll keep track of it anyways for consistency with other
    /// formats.
    pub architecture: LibArchitecture,
    /// The subdirectory within the plugins directory the orignal plugin was in. If this could not
    /// be detected then this will be `None`.
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

impl Vst2Plugin {
    /// Get the absolute path to the `.so` file we should create in `~/.vst/yabridge` for this
    /// plugin when using the centralized VST installation location mode.
    pub fn centralized_native_target(&self) -> PathBuf {
        let file_name = self
            .path
            .file_name()
            .unwrap()
            .to_str()
            .expect("Plugin name contains invalid UTF-8");
        let file_name = Path::new(file_name).with_extension("so");

        match &self.subdirectory {
            Some(directory) => yabridge_vst2_home().join(directory).join(file_name),
            None => yabridge_vst2_home().join(file_name),
        }
    }

    /// Get the absolute path to the `.dll` file we should symlink to `~/.vst/yabridge` when setting
    /// this plugin up with the centralized VST2 installation location setting.
    pub fn centralized_windows_target(&self) -> PathBuf {
        let file_name = self
            .path
            .file_name()
            .unwrap()
            .to_str()
            .expect("Plugin name contains invalid UTF-8");

        match &self.subdirectory {
            Some(directory) => yabridge_vst2_home().join(directory).join(file_name),
            None => yabridge_vst2_home().join(file_name),
        }
    }

    /// Get the absolute path to the `.so` file we should create when setting this plugin up with
    /// the inline VST2 installation location setting.
    pub fn inline_native_target(&self) -> PathBuf {
        self.path.with_extension("so")
    }
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

    /// Return the path to the Windows plugin's `moduleinfo.json` file if this is a bundle-style
    /// plugin and the plugin has one. This file would need to be rewritten using
    /// `ModuleInfo::rewrite_uid_byte_orders()` first.
    pub fn original_moduleinfo_path(&self) -> Option<PathBuf> {
        match &self.module {
            Vst3ModuleType::Bundle(bundle_home) => {
                let mut path = bundle_home.join("Contents");
                path.push("moduleinfo.json");
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

    /// Get the path to the renamed `plugin.so` file in `~/.vst3` corresponding to the bridged
    /// version of this module. The path here depends on whether we're using a 32-bit or 64-bit
    /// version of yabridge. If the configuration is not given (for instance, becuase yabridge is
    /// not set up properly) we'll assume the module should be 64-bit.
    pub fn target_native_module_path(&self, files: Option<&YabridgeFiles>) -> PathBuf {
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

        #[allow(clippy::wildcard_in_or_patterns)]
        match files.and_then(|c| c.vst3_chainloader.as_ref()) {
            Some((_, LibArchitecture::Lib32)) => path.push("i386-linux"),
            // NOTE: We'll always fall back to this if `libyabridge-chainloader-vst3.so` is not
            //       found, just so we cannot get any errors during `yabridgectl status` even if
            //       yabridge is not set up correctly.
            Some((_, LibArchitecture::Lib64)) | _ => path.push("x86_64-linux"),
        }

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

    /// If the Windows VST3 plugin had a `moduleinfo.json` file, then it should be translated using
    /// `ModuleInfo::rewrite_uid_byte_orders()` and then written to this path.
    pub fn target_moduleinfo_path(&self) -> PathBuf {
        let mut path = self.target_bundle_home();
        path.push("Contents");
        path.push("moduleinfo.json");
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

impl ClapPlugin {
    /// Get the absolute path to the `.clap` file we should create in `~/.clap/yabridge` for this
    /// plugin.
    pub fn native_target(&self) -> PathBuf {
        let file_name = self
            .path
            .file_name()
            .unwrap()
            .to_str()
            .expect("Plugin name contains invalid UTF-8");
        let file_name = Path::new(file_name).with_extension("clap");

        match &self.subdirectory {
            Some(directory) => yabridge_clap_home().join(directory).join(file_name),
            None => yabridge_clap_home().join(file_name),
        }
    }

    /// Get the absolute path to the `.clap-win` file in `~/.clap/yabrdge` the Windows `.clap`
    /// plugin should be symlinked to. This uses a different file extension so we can use the same
    /// setup as for VST2 plugins without confusing DAWs.
    pub fn windows_target(&self) -> PathBuf {
        let file_name = self
            .path
            .file_name()
            .unwrap()
            .to_str()
            .expect("Plugin name contains invalid UTF-8");
        let file_name = Path::new(file_name).with_extension("clap-win");

        match &self.subdirectory {
            Some(directory) => yabridge_clap_home().join(directory).join(file_name),
            None => yabridge_clap_home().join(file_name),
        }
    }
}

/// The architecture of a library file (either `.dll` or `.so` depending on the context). Needed so
/// we can create a merged bundle for VST3 plugins.
#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Copy)]
pub enum LibArchitecture {
    Lib32,
    Lib64,
}

impl Display for LibArchitecture {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match &self {
            LibArchitecture::Lib32 => write!(f, "32-bit"),
            LibArchitecture::Lib64 => write!(f, "64-bit"),
        }
    }
}

impl LibArchitecture {
    /// Get the corresponding VST3 architecture directory name. See
    /// https://developer.steinberg.help/display/VST/Plug-in+Format+Structure#PluginFormatStructure-FortheWindowsplatform.
    pub fn vst_arch(&self) -> &str {
        match &self {
            LibArchitecture::Lib32 => "x86-win",
            LibArchitecture::Lib64 => "x86_64-win",
        }
    }
}

impl SearchResults {
    /// Create a map out of all found Windows plugins and their current installation status, if the
    /// plugin has already been set up.
    pub fn installation_status(
        &self,
        config: &Config,
        files: Option<&YabridgeFiles>,
    ) -> BTreeMap<PathBuf, (&Plugin, Option<NativeFile>)> {
        let so_files: HashMap<&Path, &NativeFile> = self
            .so_files
            .iter()
            .map(|file| (file.path(), file))
            .collect();

        self.plugins
            .iter()
            .map(|plugin| match plugin {
                Plugin::Vst2(vst2_plugin) => {
                    // For VST2 plugins depending on the VST2 installation location setting we'll
                    // either look for a matching file in `~/.vst` or we'll just look at the
                    // similarly named `.so` file right next to the plugin `.dll` file
                    match config.vst2_location {
                        crate::config::Vst2InstallationLocation::Centralized => (
                            vst2_plugin.path.clone(),
                            (
                                plugin,
                                get_file_type(vst2_plugin.centralized_native_target()),
                            ),
                        ),
                        crate::config::Vst2InstallationLocation::Inline => {
                            match so_files.get(vst2_plugin.inline_native_target().as_path()) {
                                Some(&file_type) => {
                                    (vst2_plugin.path.clone(), (plugin, Some(file_type.clone())))
                                }
                                None => (vst2_plugin.path.clone(), (plugin, None)),
                            }
                        }
                    }
                }
                // We have not stored the paths to the corresponding `.so` files yet for VST3
                // modules because they are not in any of the directories we're indexing
                Plugin::Vst3(vst3_module) => (
                    vst3_module.original_path().to_owned(),
                    (
                        plugin,
                        get_file_type(vst3_module.target_native_module_path(files)),
                    ),
                ),
                Plugin::Clap(clap_plugin) => (
                    clap_plugin.path.clone(),
                    (plugin, get_file_type(clap_plugin.native_target())),
                ),
            })
            .collect()
    }

    /// Find all `.so` files in the search results that do not belong to a VST2 plugin `.dll` file.
    /// This depends on the VST2 installation location setting. Centralized VST2 and VST3 orphans
    /// should be detected separately.
    pub fn vst2_inline_orphans(&self, config: &Config) -> Vec<&NativeFile> {
        // We need to store these in a map so we can easily entries with corresponding `.dll` files
        let mut orphans: HashMap<&Path, &NativeFile> = self
            .so_files
            .iter()
            .map(|file_type| (file_type.path(), file_type))
            .collect();

        match config.vst2_location {
            // When we set up the plugin in `~/.vst`, any `.so` file in a VST2 plugin search
            // directory should be considered an orphan. This can happen when switching between the
            // two modes.
            crate::config::Vst2InstallationLocation::Centralized => (),
            crate::config::Vst2InstallationLocation::Inline => {
                for plugin in &self.plugins {
                    if let Plugin::Vst2(Vst2Plugin { path, .. }) = plugin {
                        orphans.remove(path.with_extension("so").as_path());
                    }
                }
            }
        }

        orphans.values().cloned().collect()
    }
}

/// Find all `.dll`, `.vst3`, `.clap`, and `.so` files under a directory. These results can be
/// filtered down to actual VST2 plugins, VST3 modules, and CLAP plugins using `search()`. Any path
/// found in the blacklist will be pruned immediately, so this can be used to both not index
/// individual files and to skip an entire directory.
///
/// For VST3 plugin _bundles_ the subdirectory also contains the `foo.vst3/Contents/x86_64-win`
/// suffix. This needs to be stripped out to get the bundle root.
pub fn index(directory: &Path, blacklist: &HashSet<&Path>) -> SearchIndex {
    // These are pairs of `(absolute_path, subdirectory)`. The subdirectory is used for setting up
    // VST3 and CLAP plugins and for setting up VST2 plugins in the centralized installation
    // location mode.
    let mut dll_files: Vec<(PathBuf, Option<PathBuf>)> = Vec::new();
    let mut vst3_files: Vec<(PathBuf, Option<PathBuf>)> = Vec::new();
    let mut clap_files: Vec<(PathBuf, Option<PathBuf>)> = Vec::new();
    let mut so_files: Vec<NativeFile> = Vec::new();
    for (file_idx, path) in WalkDir::new(directory)
        .follow_links(true)
        .into_iter()
        .filter_entry(|e| {
            // The blacklist entries are canonicalized to resolve symlinks and to normalize slashes,
            // so we should do the same thing here as well
            e.path()
                .canonicalize()
                .map(|p| !blacklist.contains(p.as_path()))
                .unwrap_or(false)
        })
        .filter_map(|e| {
            // NOTE: Broken symlinks will also get an `Err` entry, so we'll use `err.path()` to
            //       still include them in the index
            let path = match e {
                Ok(entry) => entry.path().to_owned(),
                Err(err) => err.path()?.to_owned(),
            };

            if !path.is_dir() {
                Some(path)
            } else {
                None
            }
        })
        .enumerate()
    {
        // This is a bit of an odd warning, but I can see it happening that someone adds their
        // entire home directory by accident. Removing the home directory would cause yabridgectl to
        // scan for leftover `.so` files, which would of course take an enternity. This warning will
        // at least tell the user what's happening and that they can safely cancel the scan.
        if file_idx == 100_000 {
            eprintln!(
                "Indexed over 100.000 files, press Ctrl+C to cancel this operation if this was \
                 not intentional."
            )
        }

        match path.extension().and_then(|os| os.to_str()) {
            Some("dll") => {
                let subdirectory = path
                    .parent()
                    .and_then(|p| p.strip_prefix(directory).ok())
                    .map(|p| p.to_owned());
                dll_files.push((path, subdirectory));
            }
            Some("vst3") => {
                // NOTE: For bundles this will also contain the `foo.vst3/Contents/x86_64-win`
                //       suffix. This needs to be stripped later.
                let subdirectory = path
                    .parent()
                    .and_then(|p| p.strip_prefix(directory).ok())
                    .map(|p| p.to_owned());
                vst3_files.push((path, subdirectory));
            }
            Some("clap") => {
                let subdirectory = path
                    .parent()
                    .and_then(|p| p.strip_prefix(directory).ok())
                    .map(|p| p.to_owned());
                clap_files.push((path, subdirectory));
            }
            Some("so") => {
                if path.is_symlink() {
                    so_files.push(NativeFile::Symlink(path));
                } else {
                    so_files.push(NativeFile::Regular(path));
                }
            }
            _ => (),
        }
    }

    SearchIndex {
        dll_files,
        vst3_files,
        clap_files,
        so_files,
    }
}

impl SearchIndex {
    /// Filter these indexing results down to actual VST2 plugins and VST3 modules. This will skip
    /// all invalid files, such as regular `.dll` libraries.
    pub fn search(self) -> Result<SearchResults> {
        const VST2_ENTRY_POINTS: [&str; 2] = ["VSTPluginMain", "main"];
        const VST3_ENTRY_POINTS: [&str; 1] = ["GetPluginFactory"];
        // This is a constant with external linkage, not a function
        const CLAP_ENTRY_POINTS: [&str; 1] = ["clap_entry"];

        // We'll have to figure out which `.dll` files are VST2 plugins and which should be skipped
        // by checking whether the file contains one of the VST2 entry point functions. This vector
        // will contain an `Err(path)` if `path` was not a valid VST2 plugin.
        let is_vst2_plugin: Vec<Result<Vst2Plugin, PathBuf>> = self
            .dll_files
            .into_par_iter()
            .map(|(path, subdirectory)| {
                let info = parse_pe32_binary(&path)?;
                let architecture = if info.is_64_bit {
                    LibArchitecture::Lib64
                } else {
                    LibArchitecture::Lib32
                };

                if info
                    .exports
                    .into_iter()
                    .any(|symbol| VST2_ENTRY_POINTS.contains(&symbol.as_str()))
                {
                    Ok(Ok(Vst2Plugin {
                        path,
                        architecture,
                        subdirectory,
                    }))
                } else {
                    Ok(Err(path))
                }
            })
            // Make parsing failures non-fatal. People somehow extract these `__MACOSX` and other
            // junk files from zip files containing Windows plugins created on macOS to their plugin
            // directories (how does such a thing even happen in the first place?)
            .filter_map(|result: Result<Result<Vst2Plugin, PathBuf>>| match result {
                Ok(result) => Some(result),
                Err(err) => {
                    eprintln!("WARNING: Skipping file during scan: {err:#}\n");
                    None
                }
            })
            .collect();

        // We need to do the same thing with VST3 plugins. The added difficulty here is that we have
        // to figure out of the `.vst3` file is a legacy standalone VST3 module, or part of a VST
        // 3.6.10 bundle. We also need to know the plugin's architecture because we're going to
        // create a univeral VST3 bundle.
        let is_vst3_module: Vec<Result<Vst3Module, PathBuf>> = self
            .vst3_files
            .into_par_iter()
            .map(|(module_path, subdirectory)| {
                let info = parse_pe32_binary(&module_path)?;
                let architecture = if info.is_64_bit {
                    LibArchitecture::Lib64
                } else {
                    LibArchitecture::Lib32
                };

                if info
                    .exports
                    .into_iter()
                    .any(|symbol| VST3_ENTRY_POINTS.contains(&symbol.as_str()))
                {
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

                    let (module, subdirectory) = if module_is_in_bundle {
                        (
                            Vst3ModuleType::Bundle(bundle_root.unwrap().to_owned()),
                            // The subdirectory should be relative to the bundle, not to the .vst3
                            // file inside of the bundle. The latter is what we get from the index
                            // function since it only considers regular files and symlinks.
                            subdirectory.and_then(|subdirectory| {
                                // NOTE: Just `.uwnrapping()` all of these and using `.map()`
                                //       instead of `.and_then()` should be sufficient, but for some
                                //       reason people add the `x86_64-win` directory inside of a
                                //       VST3 bundle to their plugin locations........why?
                                Some(
                                    subdirectory
                                        // x86_64-win
                                        .parent()?
                                        // Contents
                                        .parent()?
                                        // foo.vst3
                                        .parent()?
                                        .to_owned(),
                                )
                            }),
                        )
                    } else {
                        (Vst3ModuleType::Legacy(module_path), subdirectory)
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
            // See above
            .filter_map(|result: Result<Result<Vst3Module, PathBuf>>| match result {
                Ok(result) => Some(result),
                Err(err) => {
                    eprintln!("WARNING: Skipping file during scan: {err:#}\n");
                    None
                }
            })
            .collect();

        // Same for CLAP plugins
        let is_clap_plugin: Vec<Result<ClapPlugin, PathBuf>> = self
            .clap_files
            .into_par_iter()
            .map(|(path, subdirectory)| {
                let info = parse_pe32_binary(&path)?;
                let architecture = if info.is_64_bit {
                    LibArchitecture::Lib64
                } else {
                    LibArchitecture::Lib32
                };

                if info
                    .exports
                    .into_iter()
                    .any(|symbol| CLAP_ENTRY_POINTS.contains(&symbol.as_str()))
                {
                    Ok(Ok(ClapPlugin {
                        path,
                        architecture,
                        subdirectory,
                    }))
                } else {
                    Ok(Err(path))
                }
            })
            // See above
            .filter_map(|result: Result<Result<ClapPlugin, PathBuf>>| match result {
                Ok(result) => Some(result),
                Err(err) => {
                    eprintln!("WARNING: Skipping file during scan: {err:#}\n");
                    None
                }
            })
            .collect();

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
        for candidate in is_clap_plugin {
            match candidate {
                Ok(module) => plugins.push(Plugin::Clap(module)),
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
