# Keep the launcher label and executable metadata tied to project(VERSION).
enable_language(RC)
file(CONFIGURE OUTPUT "${PROJECT_BINARY_DIR}/generated/version/app_version.h" CONTENT [=[
#pragma once
namespace scr::runtime {
inline constexpr wchar_t applicationVersionLabel[] = L"v@PROJECT_VERSION@";
}
]=] @ONLY)
file(CONFIGURE OUTPUT "${PROJECT_BINARY_DIR}/player-version.rc" CONTENT [=[
#include <winver.h>
1 VERSIONINFO
FILEVERSION @PROJECT_VERSION_MAJOR@,@PROJECT_VERSION_MINOR@,@PROJECT_VERSION_PATCH@,0
PRODUCTVERSION @PROJECT_VERSION_MAJOR@,@PROJECT_VERSION_MINOR@,@PROJECT_VERSION_PATCH@,0
FILEFLAGSMASK VS_FFI_FILEFLAGSMASK
#ifdef _DEBUG
FILEFLAGS VS_FF_DEBUG
#else
FILEFLAGS 0
#endif
FILEOS VOS_NT_WINDOWS32
FILETYPE VFT_APP
FILESUBTYPE 0
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904B0"
    BEGIN
      VALUE "FileDescription", "Stunt Car Racer\0"
      VALUE "FileVersion", "@PROJECT_VERSION@\0"
      VALUE "OriginalFilename", "Stunt Car Racer.exe\0"
      VALUE "ProductName", "Stunt Car Racer\0"
      VALUE "ProductVersion", "@PROJECT_VERSION@\0"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x0409, 1200
  END
END
]=] @ONLY)
