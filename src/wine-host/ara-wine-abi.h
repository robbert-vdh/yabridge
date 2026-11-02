// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2026 Robbert van der Helm
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

#pragma once

// ARAInterface.h defines ARA_CALL as empty under GCC, which means all ARA C
// function pointers use the System V AMD64 calling convention. When running
// under Winelib, the host binary uses sysv ABI but must call Windows PE
// functions that expect the Microsoft x64 ABI. Define ARA_CALL to ms_abi
// before any ARA header is included so both outbound calls (Winelib → PE
// plugin) and inbound trampolines (PE plugin → Winelib callbacks) use the
// correct ABI. Must be included before ARAInterface.h or ARAVST3.h.
#if defined(__WINE__) && defined(WITH_ARA)
#ifndef ARA_CALL
#define ARA_CALL __attribute__((ms_abi))
#endif
#endif
