# UQM-UWP -- A port of Urquan Masters to Windows 10's UWP platform
**Status:** 
Builds and playable as a Win32 project with some minor graphics glitches

**Work Done so Far**
* Updated to build with VS 2017 and Windows SDK 14393 (Anniversary Update)
* Updated to use VCPkg versions of dependencies to make setup easy
* SDL1.2 Dependency Updated to SDL 2
* Fixed up assumptions about Windows lack of standards support that aren't valid in Windows 10

**Getting Started**
1. Install [Visual Studio 2017 Community] (https://www.visualstudio.com/downloads/)
1. Install MS [VCPkg](https://github.com/Microsoft/vcpkg)
1. Install the following packages using vcpkg:
    1. SDL2
    1. SDL2-image
    1. libvorbis
    1. libogg
    1. openal-soft
    1. zlib
1. Open the project under ./sc2/build/vs2017
1. F5 in VS
