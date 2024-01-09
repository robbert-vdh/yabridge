// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2024 Robbert van der Helm
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

use anyhow::{anyhow, bail, Context, Result};
use std::io::BufRead;
use std::{path::Path, process::Command};

use crate::util;

/// Some information parsed from a PE32(+) binary. This is needed for setting up yabridge for
/// Windows plugin libraries.
pub struct Pe32Info {
    /// Names of the symbols exported from the binary.
    pub exports: Vec<String>,
    /// Whether the binary is 64-bit (in technically, whether it's a PE32+ binary instead of PE32).
    pub is_64_bit: bool,
}

/// Check whether a PE32(+) binary exports the specified symbol. Used to detect the plugin formats
/// supported by a plugin library. Returns an error if the binary cuuld not be read, either because
/// it's not a PE32+ binary or because goblin could not read it and winedump is not installed. This
/// function will also parse non-native binaries.
pub fn parse_pe32_binary<P: AsRef<Path>>(binary: P) -> Result<Pe32Info> {
    parse_pe32_goblin(&binary).or_else(|err| {
        parse_pe32_winedump(binary)
            .with_context(|| format!("Failed to parse with both winedump and goblin: {err}"))
    })
}

/// Parse using goblin. For the 700 plugin libraries I tested this only didn't work with one of
/// them: https://github.com/m4b/goblin/issues/307
fn parse_pe32_goblin<P: AsRef<Path>>(binary: P) -> Result<Pe32Info> {
    // The original version of this function also supports ELF and Mach architectures, but we don't
    // need those things here
    let bytes = util::read(&binary)?;
    let obj = goblin::pe::PE::parse(&bytes).with_context(|| {
        format!(
            "Could not parse '{}' as a PE32(+) binary",
            binary.as_ref().display()
        )
    })?;

    Ok(Pe32Info {
        exports: obj
            .exports
            .into_iter()
            .filter_map(|export| export.name.map(String::from))
            .collect(),
        is_64_bit: obj.is_64,
    })
}

/// A fallback for if goblin can't parse a file. This is kind of a bruteforce approach and it will
/// be a lot slower, but it should also be very rare that this gets invoked at all.
fn parse_pe32_winedump<P: AsRef<Path>>(binary: P) -> Result<Pe32Info> {
    let winedump = |args: &[&str], path: &Path| {
        Command::new("winedump")
            .args(args)
            .arg(path)
            .output()
            .context(
                "Could not find 'winedump'. In some distributions this is part of a seperate Wine \
                 tools package.",
            )
            .map(|output| output.stdout)
    };

    // The previous version where we only used winedump used Aho-Corsaick automatons for more
    // efficient searching, but since this function should in theory never be called we don't even
    // try
    let basic_info = winedump(&[], binary.as_ref())?;
    let is_64_bit = basic_info
        .lines()
        .find_map(|line| match line {
            Ok(line) => {
                // NOTE: This always assumes x86 = 32-bit, and everything else = 64-bit
                let machine_type = line.trim_start().strip_prefix("Machine:")?.trim();
                if machine_type.starts_with("014C") {
                    Some(false)
                } else {
                    Some(true)
                }
            }
            Err(_) => None,
        })
        .ok_or_else(|| {
            anyhow!("Winedump output did not contain a 'Machine:' line. Is this a text file?")
        })?;

    // And we'll just parse _all_ exported functions. Previously we would only check whether this
    // contrained certain entries, but efficiency isn't too important here anyways since this is
    // only a fallback.
    let exported_functions = winedump(&["-j", "export"], binary.as_ref())?;
    let mut found_exports_table = false;
    let mut exports = Vec::new();
    for line in exported_functions.lines() {
        let line = line?;
        let line = line.trim();

        // The exports table starts after a header line and ends when reacing an empty line
        if found_exports_table {
            if line.is_empty() {
                break;
            }

            // Each line is in the format ` d34db33f 1 symbol_name`
            match line.split_ascii_whitespace().nth(2) {
                Some(export) => exports.push(String::from(export)),
                None => bail!("Malforced winedump export list line: '{line}'"),
            }
        } else if line.starts_with("Entry Pt") {
            found_exports_table = true;
        }
    }

    Ok(Pe32Info { exports, is_64_bit })
}
