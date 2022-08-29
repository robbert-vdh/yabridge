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

//! Utilities for managing yabrigectl's configuration.

use anyhow::{anyhow, Context, Result};
use rayon::prelude::*;
use serde_derive::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet, HashSet};
use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use which::which;
use xdg::BaseDirectories;

use crate::files::{self, LibArchitecture, SearchResults};
use crate::util;

/// The name of the config file, relative to `$XDG_CONFIG_HOME/YABRIDGECTL_PREFIX`.
pub const CONFIG_FILE_NAME: &str = "config.toml";
/// The name of the XDG base directory prefix for yabridgectl, relative to `$XDG_CONFIG_HOME` and
/// `$XDG_DATA_HOME`.
const YABRIDGECTL_PREFIX: &str = "yabridgectl";

/// The name of yabridge's CLAP chainloading library yabridgectl will create copies of.
pub const CLAP_CHAINLOADER_NAME: &str = "libyabridge-chainloader-clap.so";
/// The name of yabridge's VST2 chainloading library yabridgectl will create copies of.
pub const VST2_CHAINLOADER_NAME: &str = "libyabridge-chainloader-vst2.so";
/// The name of yabridge's VST3 chainloading library yabridgectl will create copies of.
pub const VST3_CHAINLOADER_NAME: &str = "libyabridge-chainloader-vst3.so";
/// The name of the script we're going to run to verify that everything's working correctly.
pub const YABRIDGE_HOST_EXE_NAME: &str = "yabridge-host.exe";
/// The 32-bit verison of `YABRIDGE_HOST_EXE_NAME`. If `~/.wine` was somehow created with
/// `WINEARCH=win32` set, then it won't be possible to run the 64-bit `yabridge-host.exe` in there.
/// In that case we'll just run the 32-bit version isntead, if it exists.
pub const YABRIDGE_HOST_32_EXE_NAME: &str = "yabridge-host-32.exe";
/// The name of the XDG base directory prefix for yabridge's own files, relative to
/// `$XDG_CONFIG_HOME` and `$XDG_DATA_HOME`.
const YABRIDGE_PREFIX: &str = "yabridge";

/// The path relative to `$HOME` that CLAP plugins bridged by yabridgectl life in. By putting this
/// in a subdirectory we can easily clean up any orphan files without interfering with other native
/// plugins.
const YABRIDGE_CLAP_HOME: &str = ".clap/yabridge";
/// The path relative to `$HOME` we will set up bridged VST2 plugins in when using the centralized
/// VST2 installation location setting. By putting this in a subdirectory we can easily clean up any
/// orphan files without interfering with other native plugins.
const YABRIDGE_VST2_HOME: &str = ".vst/yabridge";
/// The path relative to `$HOME` that VST3 modules bridged by yabridgectl life in. By putting this
/// in a subdirectory we can easily clean up any orphan files without interfering with other native
/// plugins.
const YABRIDGE_VST3_HOME: &str = ".vst3/yabridge";

/// The configuration used for yabridgectl. This will be serialized to and deserialized from
/// `$XDG_CONFIG_HOME/yabridge/config.toml`.
#[derive(Debug, Default, Deserialize, Serialize)]
#[serde(default)]
pub struct Config {
    /// The path to the directory containing `libyabridge-{chainloader,}-{clap,vst2,vst3}.so`. If not
    /// set, then yabridgectl will look in `/usr/lib` and `$XDG_DATA_HOME/yabridge` since those are
    /// the expected locations for yabridge to be installed in.
    pub yabridge_home: Option<PathBuf>,
    /// Directories to search for Windows VST plugins. These directories can contain VST2 plugin
    /// `.dll` files, VST3 modules (which should be located in `<prefix>/drive_c/Program
    /// Files/Common/VST3`), and CLAP plugins (which should similarly be installed to
    /// `<prefix>/drive_c/Program Files/Common/CLAP`). We're using an ordered set here out of
    /// convenience so we can't get duplicates and the config file is always sorted.
    pub plugin_dirs: BTreeSet<PathBuf>,
    /// Where VST2 plugins are setup. This can be either in `~/.vst/yabridge` or inline with the
    /// plugin's .dll` files.`
    pub vst2_location: Vst2InstallationLocation,
    /// Always skip post-installation setup checks. This can be set temporarily by passing the
    /// `--no-verify` option to `yabridgectl sync`.
    pub no_verify: bool,
    /// Files and directories that should be skipped during the indexing process. If this contains a
    /// directory, then everything under that directory will also be skipped. Like with
    /// `plugin_dirs`, we're using a `BTreeSet` here because it looks nicer in the config file, even
    /// though a hash set would make much more sense.
    pub blacklist: BTreeSet<PathBuf>,
    /// The last known combination of Wine and yabridge versions that would work together properly.
    /// This is mostly to diagnose issues with older Wine versions (such as those in Ubuntu's repos)
    /// early on.
    pub last_known_config: Option<KnownConfig>,
}

/// Determines where VST2 plugins are set up. They can either be set up in `~/.vst/yabridge` by
/// creating `libyabridge-chainloader-vst2.so` copies there and symlinking the Windows VST2 plugin
/// `.dll` files right next to it, or those copies can be made right next to the orignal plugin
/// files.
#[derive(Deserialize, Serialize, Debug, PartialEq, Eq, Clone, Copy)]
#[serde(rename_all = "snake_case")]
pub enum Vst2InstallationLocation {
    /// Set up the plugins in `~/.vst/yabridge`. The downside of this approach is that you cannot
    /// have multiple plugins with the same name being provided by multiple directories or prefixes.
    /// That might be useful for debugging purposes.
    Centralized,
    /// Create the `.so` files right next to the original VST2 plugins.
    Inline,
}

/// Stores information about a combination of Wine and yabridge that works together properly.
/// Whenever we encounter a new version of Wine or yabridge, we'll check whether `yabridge-host.exe`
/// can run without issues. This is needed because older versions of Wine won't be able to run newer
/// winelibs, and Ubuntu ships with old versions of Wine. To prevent repeating unnecessarily
/// repeating this check we'll keep track of the last combination of Wine and yabridge that would
/// work together properly.
#[derive(Deserialize, Serialize, Debug, PartialEq, Eq)]
pub struct KnownConfig {
    /// The output of `wine --version`, minus the trailing newline.
    pub wine_version: String,
    /// The results from running the contents of `yabridge-host.exe.so` through
    /// [`DefaultHasher`](std::collections::hash_map::DefaultHasher). Hash collisions aren't really
    /// an issue here since we mostly care about the version of Wine.
    ///
    /// This should have been stored as a `u64`, but the TOML library parses all integers as signed
    /// so even though it will be able to serialize all values correctly some values will fail to
    /// parse:
    ///
    /// https://github.com/alexcrichton/toml-rs/issues/256
    pub yabridge_host_hash: i64,
}

/// Paths to all of yabridge's files based on the `yabridge_home` setting. Created by
/// `Config::files`.
#[derive(Debug)]
pub struct YabridgeFiles {
    /// The path to `libyabridge-chainloader-vst2.so` we should use.
    pub vst2_chainloader: PathBuf,
    /// The file's architecture is used only for display purposes in `yabridgectl status`.
    pub vst2_chainloader_arch: LibArchitecture,
    /// The path to `libyabridge-chainloader-vst3.so` we should use, if yabridge has been compiled
    /// with VST3 support. We need to know if it's a 32-bit or a 64-bit library so we can properly
    /// set up the merged VST3 bundles.
    pub vst3_chainloader: Option<(PathBuf, LibArchitecture)>,
    /// The path to `libyabridge-chainloader-clap.so` we should use. The architecture is only used
    /// for display purposes in `yabridgectl status`. Because CLAP is supposed to be 64-bit-only on
    /// AMD64 systems we can also just leave this out, but it looks more consisent this way.
    /// Yabridge can be configurued without CLAP support, so this is optional.
    pub clap_chainloader: Option<(PathBuf, LibArchitecture)>,
    /// The path to `yabridge-host.exe`. This is the path yabridge will actually use, and it does
    /// not have to be relative to `yabridge_home`.
    pub yabridge_host_exe: Option<PathBuf>,
    /// The actual Winelib binary for `yabridge-host.exe`. Will be hashed to check whether the user
    /// has updated yabridge.
    pub yabridge_host_exe_so: Option<PathBuf>,
    /// The same as `yabridge_host_exe`, but for the 32-bit verison.
    pub yabridge_host_32_exe: Option<PathBuf>,
    /// The same as `yabridge_host_exe_so`, but for the 32-bit verison. We will hash this instead of
    /// there's no 64-bit version available.
    pub yabridge_host_32_exe_so: Option<PathBuf>,
}

impl Default for Vst2InstallationLocation {
    fn default() -> Self {
        Vst2InstallationLocation::Centralized
    }
}

impl Config {
    /// Try to read the config file, creating a new default file if necessary. This will fail if the
    /// file could not be created or if it could not be parsed.
    pub fn read() -> Result<Config> {
        match yabridgectl_directories()?.find_config_file(CONFIG_FILE_NAME) {
            Some(path) => {
                let toml_str = fs::read_to_string(&path).with_context(|| {
                    format!("Could not read config file at '{}'", path.display())
                })?;

                toml::from_str(&toml_str)
                    .with_context(|| format!("Failed to parse '{}'", path.display()))
            }
            None => {
                let defaults = Config::default();

                // If no existing config file exists, then write a new config file with default
                // values
                defaults.write()?;

                Ok(defaults)
            }
        }
    }

    /// Write the config to disk, creating the file if it does not yet exist.
    pub fn write(&self) -> Result<()> {
        let toml_str = toml::to_string_pretty(&self).context("Could not format TOML")?;
        let config_path = yabridgectl_directories()?
            .place_config_file(CONFIG_FILE_NAME)
            .context("Could not create config file")?;

        fs::write(&config_path, toml_str)
            .with_context(|| format!("Failed to write config file to '{}'", config_path.display()))
    }

    /// Find all of yabridge's files based on `yabridge_home`. For the binaries we'll search for
    /// them the exact same way as yabridge itself will.
    pub fn files(&self) -> Result<YabridgeFiles> {
        let xdg_dirs = yabridge_directories()?;

        // First find `libyabridge-chainloader-vst2.so`
        let vst2_chainloader: PathBuf = match &self.yabridge_home {
            Some(directory) => {
                let candidate = directory.join(VST2_CHAINLOADER_NAME);
                if candidate.exists() {
                    candidate
                } else {
                    return Err(anyhow!(
                        "Could not find '{}' in '{}'",
                        VST2_CHAINLOADER_NAME,
                        directory.display()
                    ));
                }
            }
            None => {
                // Search in the system library locations and in `~/.local/share/yabridge` if no
                // path was set explicitely. We'll also search through `/usr/local/lib` just in case
                // but since we advocate against installing yabridge there we won't list this path
                // in the error message when `libyabridge-chainloader-vst2.so` can't be found.
                let system_path = Path::new("/usr/lib");
                let user_path = xdg_dirs.get_data_home();
                let lib_directories = [
                    system_path,
                    // Used on Debian based distros
                    Path::new("/usr/lib/x86_64-linux-gnu"),
                    // Used on Fedora
                    Path::new("/usr/lib64"),
                    Path::new("/usr/local/lib"),
                    Path::new("/usr/local/lib/x86_64-linux-gnu"),
                    Path::new("/usr/local/lib64"),
                    &user_path,
                ];
                let mut candidates = lib_directories
                    .iter()
                    .map(|directory| directory.join(VST2_CHAINLOADER_NAME));
                match candidates.find(|directory| directory.exists()) {
                    Some(candidate) => candidate,
                    _ => {
                        return Err(anyhow!(
                            "Could not find '{}' in either '{}' or '{}'. You can override the \
                             default search path using 'yabridgectl set --path=<path>'.",
                            VST2_CHAINLOADER_NAME,
                            system_path.display(),
                            user_path.display()
                        ));
                    }
                }
            }
        };

        // This is displayed in `yabridgectl status`
        let vst2_chainloader_arch =
            util::get_elf_architecture(&vst2_chainloader).with_context(|| {
                format!(
                    "Could not determine ELF architecture for '{}'",
                    vst2_chainloader.display()
                )
            })?;

        // Based on that we can check if `libyabridge-chainloader-vst3.so` exists, since yabridge
        // can be compiled without VST3 support
        let vst3_chainloader = match vst2_chainloader.with_file_name(VST3_CHAINLOADER_NAME) {
            path if path.exists() => {
                // We need to know `libyabridge-chainloader-vst3.so`'s architecture to be able to
                // set up the bundle properly. 32-bit builds of yabridge are technically supported.
                let arch = util::get_elf_architecture(&path).with_context(|| {
                    format!(
                        "Could not determine ELF architecture for '{}'",
                        path.display()
                    )
                })?;

                Some((path, arch))
            }
            _ => None,
        };

        // And the same thing for `libyabridge-chainloader-clap.so`.
        let clap_chainloader = match vst2_chainloader.with_file_name(CLAP_CHAINLOADER_NAME) {
            path if path.exists() => {
                // The architecture is only used for display purposes
                let arch = util::get_elf_architecture(&path).with_context(|| {
                    format!(
                        "Could not determine ELF architecture for '{}'",
                        path.display()
                    )
                })?;

                Some((path, arch))
            }
            _ => None,
        };

        // `yabridge-host.exe` should either be in the search path, or it should be in
        // `~/.local/share/yabridge` (which was appended to the `$PATH` at the start of `main()`)
        let yabridge_host_exe = which(YABRIDGE_HOST_EXE_NAME).ok();
        let yabridge_host_exe_so = yabridge_host_exe
            .as_ref()
            .map(|path| path.with_extension("exe.so"));
        let yabridge_host_32_exe = which(YABRIDGE_HOST_32_EXE_NAME).ok();
        let yabridge_host_32_exe_so = yabridge_host_32_exe
            .as_ref()
            .map(|path| path.with_extension("exe.so"));

        Ok(YabridgeFiles {
            vst2_chainloader,
            vst2_chainloader_arch,
            vst3_chainloader,
            clap_chainloader,
            yabridge_host_exe,
            yabridge_host_exe_so,
            yabridge_host_32_exe,
            yabridge_host_32_exe_so,
        })
    }

    /// Search for VST2, VST3, and CLAP plugins in all of the registered plugins directories.
    pub fn search_directories(&self) -> Result<BTreeMap<&Path, SearchResults>> {
        let blacklist: HashSet<&Path> = self.blacklist.iter().map(|p| p.as_path()).collect();

        self.plugin_dirs
            .par_iter()
            .map(|path| {
                files::index(path, &blacklist)
                    .search()
                    .map(|search_results| (path.as_path(), search_results))
            })
            .collect()
    }
}

/// Fetch the XDG base directories for yabridge's own files, converting any error messages if this
/// somehow fails into a printable string to reduce boiler plate. This is used when searching for
/// `libyabridge-chainloader-{clap,vst2,vst3}.so` when no explicit search path has been set.
pub fn yabridge_directories() -> Result<BaseDirectories> {
    BaseDirectories::with_prefix(YABRIDGE_PREFIX).context("Error while parsing base directories")
}

/// Fetch the XDG base directories used for yabridgectl, converting any error messages if this
/// somehow fails into a printable string to reduce boiler plate.
pub fn yabridgectl_directories() -> Result<BaseDirectories> {
    BaseDirectories::with_prefix(YABRIDGECTL_PREFIX).context("Error while parsing base directories")
}

// TODO: Use `lazy_static` for these things. `$HOME` can technically change at runtime but
//       realistically it won't.

/// Get the path where bridged VST2 plugin files should be placed when using the centralized
/// installation location setting. This is a subdirectory of `~/.vst` so we can easily clean up
/// leftover files without interfering with other native plugins.
pub fn yabridge_vst2_home() -> PathBuf {
    Path::new(&env::var("HOME").expect("$HOME is not set")).join(YABRIDGE_VST2_HOME)
}

/// Get the path where VST3 modules bridged by yabridgectl should be placed in. This is a
/// subdirectory of `~/.vst3` so we can easily clean up leftover files without interfering with
/// other native plugins.
pub fn yabridge_vst3_home() -> PathBuf {
    Path::new(&env::var("HOME").expect("$HOME is not set")).join(YABRIDGE_VST3_HOME)
}

/// Get the path where CLAP modules bridged by yabridgectl should be placed in. This is a
/// subdirectory of `~/.clap` so we can easily clean up leftover files without interfering with
/// other native plugins.
pub fn yabridge_clap_home() -> PathBuf {
    Path::new(&env::var("HOME").expect("$HOME is not set")).join(YABRIDGE_CLAP_HOME)
}
