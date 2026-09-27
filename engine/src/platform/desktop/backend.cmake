# kke::platform backend: desktop and mobile (kke/Platform.h,
# docs/PLATFORMS.md). A private backend folder passed as
# -DKKE_PLATFORM_BACKEND_DIR=... contains a file like this one:
#   KKE_PLATFORM_BACKEND_SOURCES    .cpp files implementing kke/Platform.h
#   KKE_PLATFORM_BACKEND_LIBRARIES  extra libraries to link (optional)
set(KKE_PLATFORM_BACKEND_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/PlatformDesktop.cpp
)
if(WIN32)
    # bcrypt: secureRandom(); advapi32: machineId() (MachineGuid);
    # psapi: peakResidentMemoryMb().
    set(KKE_PLATFORM_BACKEND_LIBRARIES bcrypt advapi32 psapi)
else()
    set(KKE_PLATFORM_BACKEND_LIBRARIES "")
endif()
