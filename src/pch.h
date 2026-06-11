// pch.h - precompiled header
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <shlobj.h>
#include <shlguid.h>
#include <comcat.h>
#include <olectl.h>
#include <winhttp.h>
#include <commctrl.h>

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <new>
#include <cstdio>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
