/*
 * aeffectx.h - simple header to allow VeSTige compilation and eventually work
 *
 * Copyright (c) 2006 Javier Serrano Polo <jasp00/at/users.sourceforge.net>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

// This version of the header was obtained from the Audacity project
// https://github.com/audacity/audacity/blob/5900c524928be07d620bd7bfe282d53f839811f8/src/effects/VST/aeffectx.h
// and it contains a few opcodes there were missing from the original LMMS
// header

#pragma once

// Calling convention for Wine interoperability, not part of the original
// headerfrom the Audacity proejct
#ifndef __WINE__
#define __cdecl
#endif

#define VST_CALL_CONV __cdecl

#define CCONST(a, b, c, d) \
    ((((int)a) << 24) | (((int)b) << 16) | (((int)c) << 8) | (((int)d) << 0))

constexpr int audioMasterAutomate = 0;
constexpr int audioMasterVersion = 1;
constexpr int audioMasterCurrentId = 2;
constexpr int audioMasterIdle = 3;
constexpr int audioMasterPinConnected = 4;
// unsupported? 5
constexpr int audioMasterWantMidi = 6;
constexpr int audioMasterGetTime = 7;
constexpr int audioMasterProcessEvents = 8;
constexpr int audioMasterSetTime = 9;
constexpr int audioMasterTempoAt = 10;
constexpr int audioMasterGetNumAutomatableParameters = 11;
constexpr int audioMasterGetParameterQuantization = 12;
constexpr int audioMasterIOChanged = 13;
constexpr int audioMasterNeedIdle = 14;
constexpr int audioMasterSizeWindow = 15;
constexpr int audioMasterGetSampleRate = 16;
constexpr int audioMasterGetBlockSize = 17;
constexpr int audioMasterGetInputLatency = 18;
constexpr int audioMasterGetOutputLatency = 19;
constexpr int audioMasterGetPreviousPlug = 20;
constexpr int audioMasterGetNextPlug = 21;
constexpr int audioMasterWillReplaceOrAccumulate = 22;
constexpr int audioMasterGetCurrentProcessLevel = 23;
constexpr int audioMasterGetAutomationState = 24;
constexpr int audioMasterOfflineStart = 25;
constexpr int audioMasterOfflineRead = 26;
constexpr int audioMasterOfflineWrite = 27;
constexpr int audioMasterOfflineGetCurrentPass = 28;
constexpr int audioMasterOfflineGetCurrentMetaPass = 29;
constexpr int audioMasterSetOutputSampleRate = 30;
// unsupported? 31
constexpr int audioMasterGetSpeakerArrangement = 31;  // deprecated in 2.4?
constexpr int audioMasterGetVendorString = 32;
constexpr int audioMasterGetProductString = 33;
constexpr int audioMasterGetVendorVersion = 34;
constexpr int audioMasterVendorSpecific = 35;
constexpr int audioMasterSetIcon = 36;
constexpr int audioMasterCanDo = 37;
constexpr int audioMasterGetLanguage = 38;
constexpr int audioMasterOpenWindow = 39;
constexpr int audioMasterCloseWindow = 40;
constexpr int audioMasterGetDirectory = 41;
constexpr int audioMasterUpdateDisplay = 42;
constexpr int audioMasterBeginEdit = 43;
constexpr int audioMasterEndEdit = 44;
constexpr int audioMasterOpenFileSelector = 45;
constexpr int audioMasterCloseFileSelector = 46;           // currently unused
constexpr int audioMasterEditFile = 47;                    // currently unused
constexpr int audioMasterGetChunkFile = 48;                // currently unused
constexpr int audioMasterGetInputSpeakerArrangement = 49;  // currently unused

constexpr int effFlagsHasEditor = 1;
constexpr int effFlagsCanReplacing = 1 << 4;   // very likely
constexpr int effFlagsProgramChunks = 1 << 5;  // from Ardour
constexpr int effFlagsIsSynth = 1 << 8;        // currently unused

constexpr int effOpen = 0;
constexpr int effClose = 1;       // currently unused
constexpr int effSetProgram = 2;  // currently unused
constexpr int effGetProgram = 3;  // currently unused
// The next one was gleaned from
// http://www.kvraudio.com/forum/viewtopic.php?p=1905347
constexpr int effSetProgramName = 4;
constexpr int effGetProgramName = 5;  // currently unused
// The next two were gleaned from
// http://www.kvraudio.com/forum/viewtopic.php?p=1905347
constexpr int effGetParamLabel = 6;
constexpr int effGetParamDisplay = 7;
constexpr int effGetParamName = 8;  // currently unused
constexpr int effSetSampleRate = 10;
constexpr int effSetBlockSize = 11;
constexpr int effMainsChanged = 12;
constexpr int effEditGetRect = 13;
constexpr int effEditOpen = 14;
constexpr int effEditClose = 15;
constexpr int effEditIdle = 19;
constexpr int effEditTop = 20;
constexpr int effIdentify =
    22;  // from http://www.asseca.org/vst-24-specs/efIdentify.html
constexpr int effGetChunk = 23;  // from Ardour
constexpr int effSetChunk = 24;  // from Ardour
constexpr int effProcessEvents = 25;
// The next one was gleaned from
// http://www.asseca.org/vst-24-specs/efCanBeAutomated.html
constexpr int effCanBeAutomated = 26;
// The next one was gleaned from
// http://www.kvraudio.com/forum/viewtopic.php?p=1905347
constexpr int effGetProgramNameIndexed = 29;
// The next one was gleaned from
// http://www.asseca.org/vst-24-specs/efGetPlugCategory.html
constexpr int effGetPlugCategory = 35;
constexpr int effGetEffectName = 45;
constexpr int effGetParameterProperties = 56;  // missing
constexpr int effGetVendorString = 47;
constexpr int effGetProductString = 48;
constexpr int effGetVendorVersion = 49;
constexpr int effCanDo = 51;  // currently unused
// The next one was gleaned from http://www.asseca.org/vst-24-specs/efIdle.html
constexpr int effIdle = 53;
constexpr int effGetVstVersion = 58;  // currently unused
// The next one was gleaned from
// http://www.asseca.org/vst-24-specs/efBeginSetProgram.html
constexpr int effBeginSetProgram = 67;
// The next one was gleaned from
// http://www.asseca.org/vst-24-specs/efEndSetProgram.html
constexpr int effEndSetProgram = 68;
// The next one was gleaned from
// http://www.asseca.org/vst-24-specs/efShellGetNextPlugin.html
constexpr int effShellGetNextPlugin = 70;
// The next one was gleaned from
// http://www.asseca.org/vst-24-specs/efBeginLoadBank.html
constexpr int effBeginLoadBank = 75;
// The next one was gleaned from
// http://www.asseca.org/vst-24-specs/efBeginLoadProgram.html
constexpr int effBeginLoadProgram = 76;

// The next two were gleaned from
// http://www.kvraudio.com/forum/printview.php?t=143587&start=0
constexpr int effStartProcess = 71;
constexpr int effStopProcess = 72;

constexpr int kEffectMagic = CCONST('V', 's', 't', 'P');
constexpr int kVstLangEnglish = 1;
constexpr int kVstMidiType = 1;
// From
// https://github.com/x42/lv2vst/blob/30a669a021812da05258519cef9d4202f5ce26c3/include/vestige.h#L139
constexpr int kVstSysExType = 6;

constexpr int kVstNanosValid = 1 << 8;
constexpr int kVstPpqPosValid = 1 << 9;
constexpr int kVstTempoValid = 1 << 10;
constexpr int kVstBarsValid = 1 << 11;
constexpr int kVstCyclePosValid = 1 << 12;
constexpr int kVstTimeSigValid = 1 << 13;
constexpr int kVstSmpteValid = 1 << 14;  // from Ardour
constexpr int kVstClockValid = 1 << 15;  // from Ardour

constexpr int kVstTransportPlaying = 1 << 1;
constexpr int kVstTransportCycleActive = 1 << 2;
constexpr int kVstTransportChanged = 1;

class RemoteVstPlugin;

class VstMidiEvent {
   public:
    // 00
    int type;
    // 04
    int byteSize;
    // 08
    int deltaFrames;
    // 0c?
    int flags;
    // 10?
    int noteLength;
    // 14?
    int noteOffset;
    // 18
    char midiData[4];
    // 1c?
    char detune;
    // 1d?
    char noteOffVelocity;
    // 1e?
    char reserved1;
    // 1f?
    char reserved2;
};

// SysEx events weren't in Audacity's VeSTige implementation, so these are from
// https://github.com/x42/lv2vst/blob/30a669a021812da05258519cef9d4202f5ce26c3/include/vestige.h#L188
class VstMidiSysExEvent {
   public:
    int type;
    int byteSize;
    int deltaFrames;
    int flags;
    int dumpBytes;
    void* reserved1;
    char* sysexDump;
    void* reserved2;
};

class VstEvent {
   public:
    char dump[sizeof(VstMidiEvent)];
};

class VstEvents {
   public:
    // 00
    int numEvents;
    // 04
    void* reserved;
    // 08
    VstEvent* events[1];
};

// Not finished, neither really used
class VstParameterProperties {
   public:
    /* float stepFloat;
       char label[64];
       int flags;
       int minInteger;
       int maxInteger;
       int stepInteger;
       char shortLabel[8];
       int category;
       char categoryLabel[24];
       char empty[128];*/

    float stepFloat;
    float smallStepFloat;
    float largeStepFloat;
    char label[64];
    unsigned int flags;
    unsigned int minInteger;
    unsigned int maxInteger;
    unsigned int stepInteger;
    unsigned int largeStepInteger;
    char shortLabel[8];
    unsigned short displayIndex;
    unsigned short category;
    unsigned short numParametersInCategory;
    unsigned short reserved;
    char categoryLabel[24];
    char future[16];
};

#include <stdint.h>

class AEffect {
   public:
    // Never use virtual functions!!!
    // 00-03
    int magic;
    // dispatcher 04-07
    intptr_t(
        VST_CALL_CONV* dispatcher)(AEffect*, int, int, intptr_t, void*, float);
    // process, quite sure 08-0b
    void(VST_CALL_CONV* process)(AEffect*, float**, float**, int);
    // setParameter 0c-0f
    void(VST_CALL_CONV* setParameter)(AEffect*, int, float);
    // getParameter 10-13
    float(VST_CALL_CONV* getParameter)(AEffect*, int);
    // programs 14-17
    int numPrograms;
    // Params 18-1b
    int numParams;
    // Input 1c-1f
    int numInputs;
    // Output 20-23
    int numOutputs;
    // flags 24-27
    int flags;
    // Fill somewhere 28-2b
    void* ptr1;
    void* ptr2;
    int initialDelay;
    // Zeroes 34-37 38-3b
    int empty3a;
    int empty3b;
    // 1.0f 3c-3f
    float unkown_float;
    // An object? pointer 40-43
    void* ptr3;
    // Zeroes 44-47
    void* user;
    // Id 48-4b
    int uniqueID;
    int version;
    // processReplacing 50-53
    void(VST_CALL_CONV* processReplacing)(AEffect*, float**, float**, int);
    // Found at
    // https://git.iem.at/zmoelnig/VeSTige/-/blob/b0e67183e155fec32dd85a2c7b5c2e4b58407323/vestige.h#L323
    // The offset was also found based on a segfualt in REAPER's audio audio
    // engine when it tried to call this function for the Rx7 plugins when
    // yabridge did not yet implement it
    void(VST_CALL_CONV* processDoubleReplacing)(AEffect*,
                                                double**,
                                                double**,
                                                int);
};

class VstTimeInfo {
   public:
    // 00
    double samplePos;
    // 08
    double sampleRate;
    // 10
    double nanoSeconds;
    // 18
    double ppqPos;
    // 20?
    double tempo;
    // 28
    double barStartPos;
    // 30?
    double cycleStartPos;
    // 38?
    double cycleEndPos;
    // 40?
    int timeSigNumerator;
    // 44?
    int timeSigDenominator;
    // unconfirmed 48 4c 50
    char empty3[4 + 4 + 4];
    // 54
    int flags;
};

typedef intptr_t(VST_CALL_CONV* audioMasterCallback)(AEffect*,
                                                     int,
                                                     int,
                                                     intptr_t,
                                                     void*,
                                                     float);

// from http://www.asseca.org/vst-24-specs/efGetParameterProperties.html
enum VstParameterFlags {
    kVstParameterIsSwitch = 1 << 0,           // parameter is a switch (on/off)
    kVstParameterUsesIntegerMinMax = 1 << 1,  // minInteger, maxInteger valid
    kVstParameterUsesFloatStep =
        1 << 2,  // stepFloat, smallStepFloat, largeStepFloat valid
    kVstParameterUsesIntStep = 1 << 3,  // stepInteger, largeStepInteger valid
    kVstParameterSupportsDisplayIndex = 1 << 4,     // displayIndex valid
    kVstParameterSupportsDisplayCategory = 1 << 5,  // category, etc. valid
    kVstParameterCanRamp = 1 << 6  // set if parameter value can ramp up/down
};

// from http://www.asseca.org/vst-24-specs/efBeginLoadProgram.html
struct VstPatchChunkInfo {
    int version;         // Format Version (should be 1)
    int pluginUniqueID;  // UniqueID of the plug-in
    int pluginVersion;   // Plug-in Version
    int numElements;     // Number of Programs (Bank) or Parameters (Program)
    char future[48];     // Reserved for future use
};

// from http://www.asseca.org/vst-24-specs/efGetPlugCategory.html
enum VstPlugCategory {
    kPlugCategUnknown = 0,     // 0=Unknown, category not implemented
    kPlugCategEffect,          // 1=Simple Effect
    kPlugCategSynth,           // 2=VST Instrument (Synths, samplers,...)
    kPlugCategAnalysis,        // 3=Scope, Tuner, ...
    kPlugCategMastering,       // 4=Dynamics, ...
    kPlugCategSpacializer,     // 5=Panners, ...
    kPlugCategRoomFx,          // 6=Delays and Reverbs
    kPlugSurroundFx,           // 7=Dedicated surround processor
    kPlugCategRestoration,     // 8=Denoiser, ...
    kPlugCategOfflineProcess,  // 9=Offline Process
    kPlugCategShell,      // 10=Plug-in is container of other plug-ins  @see
                          // effShellGetNextPlugin()
    kPlugCategGenerator,  // 11=ToneGenerator, ...
    kPlugCategMaxCount    // 12=Marker to count the categories
};

class VstRect {
   public:
    short top;
    short left;
    short bottom;
    short right;
};
