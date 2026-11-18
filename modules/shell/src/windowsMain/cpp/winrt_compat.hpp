#pragma once

#include <unknwn.h>
#include <windows.h>

// C++/WinRT needs IUnknown defined before its base header for classic COM
// GUIDs. The legacy Win32 macro also collides with Windows and Microsoft XAML
// methods.
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/base.h>
