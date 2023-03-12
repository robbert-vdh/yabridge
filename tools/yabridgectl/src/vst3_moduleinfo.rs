// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2023 Robbert van der Helm
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

use anyhow::{Context, Result};
use serde_derive::{Deserialize, Serialize};
use std::fmt::Write;

/// Part of the VST3 `moduleinfo.json` file:
/// <https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/VST+Module+Architecture/ModuleInfo-JSON.html>
///
/// Since we only need this to rewrite the UIDs, all the other fields are stored in this `other`
/// map. And while this is technically supposed to be JSON5, `serde_jsonrc` can also parse trailing
/// commands and comments and works a lot better.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ModuleInfo {
    #[serde(rename = "Classes")]
    classes: Vec<Class>,
    // The whole point of moduleinfo.json is so plugins can provide these compatibility mappings,
    // but apparently there are now plugins that have a moduleinfo file without compatibility
    // mappings
    #[serde(skip_serializing_if = "Option::is_none")]
    #[serde(rename = "Compatibility")]
    compatibility_mappings: Option<Vec<CompatibilityMapping>>,
    #[serde(flatten)]
    other: serde_jsonrc::Map<String, serde_jsonrc::Value>,
}

/// A single class object, we only care about the CID since we need to rewrite those.
#[derive(Debug, Clone, Serialize, Deserialize)]
struct Class {
    #[serde(rename = "CID")]
    cid: String,
    #[serde(flatten)]
    other: serde_jsonrc::Map<String, serde_jsonrc::Value>,
}

/// A mapping from old class IDs to new class IDs.
#[derive(Debug, Clone, Serialize, Deserialize)]
struct CompatibilityMapping {
    #[serde(rename = "New")]
    new: String,
    #[serde(rename = "Old")]
    old: Vec<String>,
    // This will probably stay empty, but let's add it just in case the format changes.
    #[serde(flatten)]
    other: serde_jsonrc::Map<String, serde_jsonrc::Value>,
}

impl ModuleInfo {
    /// Rewrite the module info in place to switch between COM-style class ID byte orders and the
    /// other style used on Linux and macOS. This is needed for cross platform plugin compatibility,
    /// because someone at Steinberg was a genius.
    pub fn rewrite_uid_byte_orders(&mut self) -> Result<()> {
        for class in &mut self.classes {
            class.cid = encode_hex_uid(&rewrite_uid_byte_order(&decode_hex_uid(&class.cid)?));
        }

        if let Some(compatibility_mappings) = &mut self.compatibility_mappings {
            for mapping in compatibility_mappings {
                mapping.new =
                    encode_hex_uid(&rewrite_uid_byte_order(&decode_hex_uid(&mapping.new)?));
                for cid in &mut mapping.old {
                    *cid = encode_hex_uid(&rewrite_uid_byte_order(&decode_hex_uid(cid)?))
                }
            }
        }

        Ok(())
    }
}

/// Parse a hexadecimal UID from a string. Returns an error if the parsing failed.
fn decode_hex_uid(hex_uid: &str) -> Result<[u8; 16]> {
    if hex_uid.len() != 32 {
        anyhow::bail!("Incorrect UID hex string length: {hex_uid:?}");
    }

    // `u8::from_str_radix` only works with str slices, and there's no way to iterate over strings
    // in str slices, so iterating over indices and manually slicing is the only solution ehre
    let mut uid = [0; 16];
    for (idx, uid_byte) in uid.iter_mut().enumerate() {
        let start_idx = idx * 2;
        let end_idx = start_idx + 2;
        *uid_byte = u8::from_str_radix(&hex_uid[start_idx..end_idx], 16)
            .with_context(|| format!("Invalid hexadecimal string: {hex_uid:?}"))?;
    }

    Ok(uid)
}

/// Format a UID stored in a byte array as a 16 character hexadecimal string.
fn encode_hex_uid(uid: &[u8; 16]) -> String {
    let mut hex_uid = String::with_capacity(uid.len() * 2);
    for b in uid {
        write!(&mut hex_uid, "{:02X}", b).unwrap();
    }

    hex_uid
}

/// Switch between the COM and non-COM byte orders for a UID.
fn rewrite_uid_byte_order(old_uid: &[u8; 16]) -> [u8; 16] {
    let mut new_uid = *old_uid;

    new_uid[0] = old_uid[3];
    new_uid[1] = old_uid[2];
    new_uid[2] = old_uid[1];
    new_uid[3] = old_uid[0];

    new_uid[4] = old_uid[5];
    new_uid[5] = old_uid[4];
    new_uid[6] = old_uid[7];
    new_uid[7] = old_uid[6];

    new_uid
}
