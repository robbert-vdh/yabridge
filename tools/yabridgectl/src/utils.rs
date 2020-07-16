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

//! Small helper utilities.

use colored::Colorize;
use std::env;
use std::path::Path;
use std::process::{Command, Stdio};
use textwrap::Wrapper;

/// Verify that `yabridge-host.exe` is accessible in a login shell. Returns unit if it is, or if we
/// the login shell is set to an unknown shell. In the last case we'll just print a warning since we
/// don't know how to invoke the shell as a login shell. This is needed when using copies to ensure
/// that yabridge can find the host binaries when the VST host is launched from the desktop
/// enviornment.
///
/// When we could not find `yabridge-host.exe`, we'll return `Err(shell_name)` so we can print a
/// descriptive warning message.
///
/// # TODO
///
/// Starting from Rust 1.45 we can just modify `argv[0]` to start with a dash instead, see
/// https://doc.rust-lang.org/std/os/unix/process/trait.CommandExt.html#tymethod.arg0
/// https://github.com/rust-lang/rust/issues/66510
pub fn verify_path_setup() -> Result<(), String> {
    match env::var("SHELL") {
        Ok(shell_path) => {
            // `$SHELL` will often contain a full path, but it doesn't have to
            let shell = Path::new(&shell_path)
                .file_name()
                .and_then(|os_str| os_str.to_str())
                .unwrap_or_else(|| shell_path.as_str());

            let mut command = Command::new(&shell_path);
            let command = match shell {
                // All of these shells support the `-l` flag to start a login shell and
                // "-c<command>" to directly run a command under that login shell
                "ash" | "bash" | "csh" | "ksh" | "dash" | "fish" | "sh" | "tcsh" | "zsh" => command
                    .arg("-l")
                    .arg("-c")
                    .arg("command -v yabridge-host.exe"),
                // I don't know if anyone uses PowerShell as their login shell under Linux, but it
                // doesn't implement the POSIX `command` function so we'll just use which instead
                "pwsh" => command.arg("-l").arg("-c").arg("which yabridge-host.exe"),
                shell => {
                    eprintln!(
                        "{}",
                        wrap(&format!(
                            "WARNING: Yabridgectl does not know how to handle your login shell, \
                             '{}', skipping PATH setup check. Feel free to open a bug report to \
                             get yabridgectl to support your shell.\n\
                             \n\
                             https://github.com/robbert-vdh/yabridge/issues",
                            shell.bright_white(),
                        ))
                    );
                    return Ok(());
                }
            };

            // For the login shell we want to a clean environment, but we still have to set `$HOME`
            // or else most shells won't know which profile to load
            command
                .env_clear()
                .env("HOME", env::var("HOME").unwrap_or_default());

            match command.stdout(Stdio::null()).stderr(Stdio::null()).status() {
                Ok(status) if status.success() => Ok(()),
                Ok(_) => Err(shell.to_string()),
                Err(err) => {
                    eprintln!(
                        "Warning: could not run login shell, skipping PATH setup check: {}",
                        err
                    );
                    Ok(())
                }
            }
        }
        Err(_) => {
            eprintln!("\nWarning: Could not determine login shell, skipping PATH setup check");
            Ok(())
        }
    }
}

/// Wrap a long paragraph of text to terminal width, or 80 characters if the width of the terminal
/// can't be determined. Everything after the first line gets indented with four spaces.
pub fn wrap(text: &str) -> String {
    let wrapper = Wrapper::with_termwidth().subsequent_indent("    ");

    wrapper.fill(text)
}
