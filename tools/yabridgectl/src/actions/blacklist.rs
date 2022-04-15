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

//! Handlers for the blacklist subcommands, just to keep `main.rs` clean.

use anyhow::Result;
use std::path::{Path, PathBuf};

use crate::config::Config;

/// Add a path to the blacklist. Duplicates get ignord because we're using ordered sets.
pub fn add_path(config: &mut Config, path: PathBuf) -> Result<()> {
    config.blacklist.insert(path);
    config.write()
}

/// Remove a path from the blacklist. The path is assumed to be part of `config.blacklist`,
/// otherwise this is silently ignored.
pub fn remove_path(config: &mut Config, path: &Path) -> Result<()> {
    // We've already verified that this path is in `config.blacklist`
    config.blacklist.remove(path);
    config.write()
}

/// List the paths in the blacklist.
pub fn list_paths(config: &Config) -> Result<()> {
    for directory in &config.blacklist {
        println!("{}", directory.display());
    }

    Ok(())
}

/// Clear the entire blacklist.
pub fn clear(config: &mut Config) -> Result<()> {
    config.blacklist.clear();
    config.write()
}
