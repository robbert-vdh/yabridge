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

//! Utilities for managing yabrigectl's configuration.

use anyhow::{anyhow, Context, Result};
use rayon::prelude::*;
use serde_derive::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet};
use std::env;
use std::fmt::Display;
use std::fs;
use std::path::{Path, PathBuf};
use which::which;
use xdg::BaseDirectories;

use crate::files::{self, SearchResults};

/// The name of the config file, relative to `$XDG_CONFIG_HOME/YABRIDGECTL_PREFIX`.
pub const CONFIG_FILE_NAME: &str = "config.toml";
/// The name of the XDG base directory prefix for yabridgectl, relative to `$XDG_CONFIG_HOME` and
/// `$XDG_DATA_HOME`.
const YABRIDGECTL_PREFIX: &str = "yabridgectl";

/// The name of yabridge's VST2 library.
pub const LIBYABRIDGE_VST2_NAME: &str = "libyabridge-vst2.so";
/// The name of yabridge's VST3 library.
pub const LIBYABRIDGE_VST3_NAME: &str = "libyabridge-vst3.so";
/// The name of the script we're going to run to verify that everything's working correctly.
pub const YABRIDGE_HOST_EXE_NAME: &str = "yabridge-host.exe";
/// The name of the XDG base directory prefix for yabridge's own files, relative to
/// `$XDG_CONFIG_HOME` and `$XDG_DATA_HOME`.
const YABRIDGE_PREFIX: &str = "yabridge";

/// The path relative to `$HOME` that VST3 modules bridged by yabridgectl life in. By putting this
/// in a subdirectory we can easily clean up any orphan files without interfering with other native
/// plugins.
const YABRIDGE_VST3_HOME: &str = ".vst3/yabridge";

/// The configuration used for yabridgectl. This will be serialized to and deserialized from
/// `$XDG_CONFIG_HOME/yabridge/config.toml`.
#[derive(Deserialize, Serialize, Debug)]
pub struct Config {
    /// The installation method to use. We will default to creating copies since that works
    /// everywhere.
    pub method: InstallationMethod,
    /// The path to the directory containing `libyabridge-{vst2,vst3}.so`. If not set, then
    /// yabridgectl will look in `/usr/lib` and `$XDG_DATA_HOME/yabridge` since those are the
    /// expected locations for yabridge to be installed in.
    pub yabridge_home: Option<PathBuf>,
    /// Directories to search for Windows VST plugins. These directories can contain both VST2
    /// plugin `.dll` files and VST3 modules (which should be located in `<prefix>/drive_c/Program
    /// Files/Common/VST3`). We're using an ordered set here out of convenience so we can't get
    /// duplicates and the config file is always sorted.
    pub plugin_dirs: BTreeSet<PathBuf>,
    /// The last known combination of Wine and yabridge versions that would work together properly.
    /// This is mostly to diagnose issues with older Wine versions (such as those in Ubuntu's repos)
    /// early on.
    pub last_known_config: Option<KnownConfig>,
}

/// Specifies how yabridge will be set up for the found plugins.
#[derive(Deserialize, Serialize, Debug, PartialEq, Eq, Clone, Copy)]
#[serde(rename_all = "snake_case")]
pub enum InstallationMethod {
    /// Create a copy of `libyabridge-{vst2,vst3}.so` for every Windows VST2 plugin `.dll` file or
    /// VST3 module found. After updating yabridge, the user will have to rerun `yabridgectl sync`
    /// to copy over the new version.
    Copy,
    /// This will create a symlink to `libyabridge-{vst2,vst3}.so` for every VST2 plugin `.dll` file
    /// or VST3 module in the plugin directories. Now that yabridge also searches in
    /// `~/.local/share/yabridge` since yabridge 2.1 this option is not really needed anymore.
    Symlink,
}

impl InstallationMethod {
    /// The plural term for this installation methodd, using in string formatting.
    pub fn plural_name(&self) -> &str {
        match &self {
            InstallationMethod::Copy => "copies",
            InstallationMethod::Symlink => "symlinks",
        }
    }
}

impl Display for InstallationMethod {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match &self {
            InstallationMethod::Copy => write!(f, "copy"),
            InstallationMethod::Symlink => write!(f, "symlink"),
        }
    }
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
    /// The path to `libyabridge-vst2.so` we should use.
    pub libyabridge_vst2: PathBuf,
    /// The path to `libyabridge-vst3.so` we should use, if yabridge has been compiled with VST3
    /// support.
    pub libyabridge_vst3: Option<PathBuf>,
    /// The path to `yabridge-host.exe`. This is the path yabridge will actually use, and it does
    /// not have to be relative to `yabridge_home`.
    pub yabridge_host_exe: PathBuf,
    /// The actual Winelib binary for `yabridge-host.exe`. Will be hashed to check whether the user
    /// has updated yabridge.
    pub yabridge_host_exe_so: PathBuf,
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
                let defaults = Config {
                    method: InstallationMethod::Copy,
                    yabridge_home: None,
                    plugin_dirs: BTreeSet::new(),
                    last_known_config: None,
                };

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

        // First find `libyabridge-vst2.so`
        let libyabridge_vst2: PathBuf = match &self.yabridge_home {
            Some(directory) => {
                let candidate = directory.join(LIBYABRIDGE_VST2_NAME);
                if candidate.exists() {
                    candidate
                } else {
                    return Err(anyhow!(
                        "Could not find '{}' in '{}'",
                        LIBYABRIDGE_VST2_NAME,
                        directory.display()
                    ));
                }
            }
            None => {
                // Search in the two common installation locations if no path was set explicitely.
                // We'll also search through `/usr/local/lib` just in case but since we advocate
                // against isntalling yabridge there we won't list this path in the error message
                // when `libyabridge-vst2.so` can't be found.
                let system_path = Path::new("/usr/lib");
                let system_path_alt = Path::new("/usr/local/lib");
                let user_path = xdg_dirs.get_data_home();
                let directories = [system_path, system_path_alt, &user_path];
                let mut candidates = directories
                    .iter()
                    .map(|directory| directory.join(LIBYABRIDGE_VST2_NAME));
                match candidates.find(|directory| directory.exists()) {
                    Some(candidate) => candidate,
                    _ => {
                        return Err(anyhow!(
                            "Could not find '{}' in either '{}' or '{}'. You can override the \
                            default search path using 'yabridgectl set --path=<path>'.",
                            LIBYABRIDGE_VST2_NAME,
                            system_path.display(),
                            user_path.display()
                        ));
                    }
                }
            }
        };

        // Based on that we can check if `libyabridge-vst3.so` exists, since yabridge can be
        // compiled without VST3 support
        let libyabridge_vst3 = match libyabridge_vst2.with_file_name(LIBYABRIDGE_VST3_NAME) {
            path if path.exists() => Some(path),
            _ => None,
        };

        // `yabridge-host.exe` should either be in the search path, or it should be in
        // `~/.local/share/yabridge`
        let yabridge_host_exe = match which(YABRIDGE_HOST_EXE_NAME)
            .ok()
            .or_else(|| xdg_dirs.find_data_file(YABRIDGE_HOST_EXE_NAME))
        {
            Some(path) => path,
            _ => {
                return Err(anyhow!("Could not locate '{}'.", YABRIDGE_HOST_EXE_NAME));
            }
        };
        let yabridge_host_exe_so = yabridge_host_exe.with_extension("exe.so");

        Ok(YabridgeFiles {
            libyabridge_vst2,
            libyabridge_vst3,
            yabridge_host_exe,
            yabridge_host_exe_so,
        })
    }

    /// Search for VST2 and VST3 plugins in all of the registered plugins directories. This will
    /// return an error if `winedump` could not be called.
    pub fn search_directories(&self) -> Result<BTreeMap<&Path, SearchResults>> {
        self.plugin_dirs
            .par_iter()
            .map(|path| {
                files::index(path)
                    .search()
                    .map(|search_results| (path.as_path(), search_results))
            })
            .collect()
    }
}

/// Fetch the XDG base directories for yabridge's own files, converting any error messages if this
/// somehow fails into a printable string to reduce boiler plate. This is only used when searching
/// for `libyabridge-{vst2,vst3}.so` when no explicit search path has been set.
pub fn yabridge_directories() -> Result<BaseDirectories> {
    BaseDirectories::with_prefix(YABRIDGE_PREFIX).context("Error while parsing base directories")
}

/// Fetch the XDG base directories used for yabridgectl, converting any error messages if this
/// somehow fails into a printable string to reduce boiler plate.
pub fn yabridgectl_directories() -> Result<BaseDirectories> {
    BaseDirectories::with_prefix(YABRIDGECTL_PREFIX).context("Error while parsing base directories")
}

/// Get the path where VST3 modules bridged by yabridgectl should be placed in. This is a
/// subdirectory of `~/.vst3` so we can easily clean up leftover files without interfering with
/// other native plugins.
pub fn yabridge_vst3_home() -> PathBuf {
    Path::new(&env::var("HOME").expect("$HOME is not set")).join(YABRIDGE_VST3_HOME)
}
