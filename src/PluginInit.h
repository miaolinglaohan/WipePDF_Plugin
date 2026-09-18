#pragma once

#ifndef WIN_PLATFORM
#define WIN_PLATFORM 1
#endif

#ifndef WIN_ENV
#define WIN_ENV 1
#endif

#ifndef WIN32
#define WIN32 1
#endif

#ifndef PLATFORM
#define PLATFORM "winpltfm.h"
#endif

#ifndef PRODUCT
#define PRODUCT "Plugin.h"
#endif

#ifndef PLUGIN
#define PLUGIN 1
#endif

#define HAS_ACROBAT_SDK 1

#include "PIHeaders.h"

#ifdef __cplusplus
extern "C" {
#endif

extern ExtensionID gExtensionID;

#ifdef __cplusplus
}
#endif
