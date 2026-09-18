#pragma once

#ifndef WIN_PLATFORM
#define WIN_PLATFORM 1
#endif

#ifndef WIN32
#define WIN32 1
#endif

#include <windows.h>

// Attempt to include official Acrobat SDK header if available
#if __has_include(<PIHeaders.h>)
#include <PIHeaders.h>
#define HAS_ACROBAT_SDK 1
#elif __has_include("PIHeaders.h")
#include "PIHeaders.h"
#define HAS_ACROBAT_SDK 1
#else
#define HAS_ACROBAT_SDK 0

// Minimal Acrobat types and callback definitions for standalone development & compilation
typedef void* ASCallback;
typedef void* AVMenu;
typedef void* AVMenuItem;
typedef void* AVDoc;
typedef void* PDDoc;
typedef void* PDPage;
typedef void* PDEContent;
typedef void* PDEElement;
typedef void* CosObj;
typedef void* CosDoc;
typedef int ASInt32;
typedef unsigned int ASUns32;
typedef double ASReal;
typedef int ASBool;

#define TRUE 1
#define FALSE 0
#define ACCALLBACK __cdecl

#endif
