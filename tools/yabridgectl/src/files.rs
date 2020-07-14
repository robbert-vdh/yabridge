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
use std::collections::{BTreeMap, HashMap};
use std::path::{Path, PathBuf};
use std::process::Command;
use walkdir::WalkDir;

/// Stores the results from searching for Windows VST plugin `.dll` files and native Linux `.so`
/// files inside of a directory. These `.so` files are kept track of so we can report the current
/// installation status and to be able to prune orphan files.
#[derive(Debug)]
pub struct SearchResults {
    /// Absolute paths to the found VST2 `.dll` files.
    pub vst2_files: Vec<PathBuf>,
    /// The number of skipped `.dll` files. Only used for printing statistics, so we don't keep
    /// track of the exact files.
    pub num_skipped_files: usize,
    /// Absolute paths to any `.so` files inside of the directory, and whether they're a symlink or
    /// a regular file.
    pub so_files: Vec<FoundFile>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum FoundFile {
    Symlink(PathBuf),
    Regular(PathBuf),
}

impl SearchResults {
    /// For every found VST2 plugin, find the associated copy or symlink of `libyabridge.so`. The
    /// returned hashmap will contain a `None` value for plugins that have not yet been set up.
    ///
    /// These two functions could be combined into a single function, but speed isn't really an
    /// issue here and it's a bit more organized this way.
    pub fn installation_status(&self) -> BTreeMap<&Path, Option<&FoundFile>> {
        let so_files: HashMap<&Path, &FoundFile> = self
            .so_files
            .iter()
            .map(|file| (file.path(), file))
            .collect();

        self.vst2_files
            .iter()
            .map(
                |path| match so_files.get(path.with_extension("so").as_path()) {
                    Some(&file) => (path.as_path(), Some(file)),
                    None => (path.as_path(), None),
                },
            )
            .collect()
    }

    /// Find all `.so` files in the search results that do not belong to a VST2 plugin `.dll` file.
    pub fn orphans(&self) -> Vec<&FoundFile> {
        // We need to store these in a map so we can easily entries with corresponding `.dll` files
        let mut orphans: HashMap<&Path, &FoundFile> = self
            .so_files
            .iter()
            .map(|file| (file.path(), file))
            .collect();
        for vst2_path in &self.vst2_files {
            orphans.remove(vst2_path.with_extension("so").as_path());
        }

        orphans.into_iter().map(|(_, file)| file).collect()
    }
}

impl FoundFile {
    /// Return the path of a found `.so` file.
    pub fn path(&self) -> &Path {
        match &self {
            FoundFile::Symlink(path) => path,
            FoundFile::Regular(path) => path,
        }
    }
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

    let dll_file_count = dll_files.len();
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
        num_skipped_files: dll_file_count - vst2_files.len(),
        vst2_files,
        so_files,
    })
}
