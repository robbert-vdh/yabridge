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
use lazy_static::lazy_static;
use rayon::prelude::*;
use std::path::{Path, PathBuf};
use std::process::Command;
use walkdir::WalkDir;

/// Stores the results from searching for Windows VST plugin `.dll` files and native Linux `.so`
/// files inside of a directory. These `.so` files are kept track of so we can report the current
/// installation status and to be able to purge orphan files.
#[derive(Debug)]
pub struct SearchResults {
    /// Absolute paths to the found VST2 `.dll` files.
    pub vst2_files: Vec<PathBuf>,
    /// Absolute paths to any `.so` files inside of the directory, and whether they're a symlink or
    /// a regular file.
    pub so_files: Vec<FoundFile>,
}

#[derive(Debug)]
pub enum FoundFile {
    Symlink(PathBuf),
    Regular(PathBuf),
}

/// Search for Windows VST2 plugins and .so files under a directory. This will return an error if
/// the directory does not exist, or if `winedump` could not be found.
pub fn index(directory: &Path) -> Result<SearchResults, std::io::Error> {
    // First we'll find all .dll and .so files in the directory
    let mut dll_files: Vec<PathBuf> = Vec::new();
    let mut so_files: Vec<FoundFile> = Vec::new();
    // XXX: We're silently skipping directories and files we don't have permission to read. This
    //      sounds like the expected behavior, but I"m not entirely sure.
    for entry in WalkDir::new(directory)
        .follow_links(true)
        .into_iter()
        .filter_map(|e| e.ok())
        .filter(|x| !x.file_type().is_dir())
    {
        match entry.path().extension().and_then(|os| os.to_str()) {
            Some("dll") => dll_files.push(entry.into_path()),
            Some("so") => {
                if entry.path_is_symlink() {
                    so_files.push(FoundFile::Symlink(entry.into_path()));
                } else {
                    so_files.push(FoundFile::Regular(entry.into_path()));
                }
            }
            _ => (),
        }
    }

    // Then we'll filter out any .dll files that are not VST2 plugins by checking whether the
    // exptected entry points for VST2 plugins are present
    lazy_static! {
        static ref VST2_AUTOMATON: AhoCorasick =
            AhoCorasick::new_auto_configured(&["VSTPluginMain", "main", "main_plugin"]);
    }

    let vst2_files: Vec<PathBuf> = dll_files
        .into_par_iter()
        .map(|path| {
            let exported_functions = Command::new("winedump")
                .arg("-j")
                .arg("export")
                .arg(&path)
                .output()?
                .stdout;

            Ok((path, exported_functions))
        })
        .filter_map(|result| match result {
            Ok((path, exported_functions)) => {
                if VST2_AUTOMATON.is_match(exported_functions) {
                    Some(Ok(path))
                } else {
                    None
                }
            }
            Err(err) => Some(Err(err)),
        })
        .collect::<Result<_, std::io::Error>>()?;

    Ok(SearchResults {
        vst2_files,
        so_files,
    })
}
