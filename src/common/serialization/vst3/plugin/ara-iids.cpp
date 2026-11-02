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

// Provides the out-of-line definitions for ARA::IPlugInEntryPoint::iid,
// ARA::IPlugInEntryPoint2::iid, and ARA::IMainFactory::iid.
// DECLARE_CLASS_IID only emits the definition in some build configurations;
// DEF_CLASS_IID always emits it.

#ifdef WITH_ARA

#include <pluginterfaces/base/funknown.h>
#include <ARAVST3.h>

DEF_CLASS_IID(ARA::IMainFactory)
DEF_CLASS_IID(ARA::IPlugInEntryPoint)
DEF_CLASS_IID(ARA::IPlugInEntryPoint2)

#endif
