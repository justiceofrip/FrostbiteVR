# BF4 adapter for FrostbiteVR. Include from the root CMakeLists.txt after FvrCore:
#   include(cmake/Bf4Adapter.cmake)
# Builds only for x64 (bf4.exe is x64; bf4_x86.exe is not supported).
#
# Disk budget: one Release tree, one test executable, no per-test binaries. The adapter
# keeps a PDB (crash triage, AGENTS.md); tests and the emulator do not.

option(FVR_BF4 "Build the Battlefield 4 adapter" ON)
option(FVR_BF4_EMU_TEST "Register the emulated BF4 end-to-end test (WARP, no game/GPU/HMD)" ON)
option(FVR_BF4_FRAMEWORK_GLUE "Theater link: BF4 adapter <-> XR host over the menu lane (FvrFrameChannel/FvrD3D11FrameBridge)" ON)

if(NOT FVR_BF4)
  return()
endif()

# Platform-neutral core: compiles anywhere, covered by Bf4CoreTests.
add_library(FvrBf4Core STATIC
  src/game/Capability.cpp
  src/interaction/ReloadPolicy.cpp
  src/interaction/Holsters.cpp
  src/interaction/VirtualPad.cpp
  src/runtime/ExactWrite.cpp
  src/ui/ModMenu.cpp
  src/engines/frostbite/PatternScan.cpp
  src/engines/frostbite/Fb3CameraMath.cpp
  src/games/bf4/Bf4Bindings.cpp
  src/games/bf4/Bf4Profile.cpp
  src/games/bf4/Bf4SessionGuard.cpp
  src/games/bf4/Bf4SessionInfo.cpp
  src/games/bf4/Bf4PointerInjector.cpp
  src/games/bf4/Bf4AdapterCore.cpp
  src/games/bf4/Bf4ModMenu.cpp
  src/games/bf4/Bf4ResearchControl.cpp
  src/games/bf4/Bf4ViewOverride.cpp
  src/games/bf4/Bf4EngineStereo.cpp
  src/games/bf4/Bf4EyeLoopOverride.cpp
  src/games/bf4/Bf4ReloadClassifier.cpp
  src/games/bf4/Bf4ReloadDriver.cpp
  src/games/bf4/stereo/NvapiStereoEmu.cpp
  src/games/bf4/stereo/NvapiCoreEmu.cpp
  src/games/bf4/stereo/EyeSequence.cpp
  src/games/bf4/stereo/StereoProbe.cpp
  src/games/bf4/stereo/SideBySide.cpp)
target_include_directories(FvrBf4Core PUBLIC include)
# Framework input types (ControllerInput / ValidInput) for the virtual gamepad.
target_link_libraries(FvrBf4Core PUBLIC FvrCore)
target_compile_features(FvrBf4Core PUBLIC cxx_std_20)

if(BUILD_TESTING)
  # One executable for every platform-neutral suite, including the headless emulated
  # sessions (fake platform + synthetic .text image driving the real AdapterCore).
  add_executable(Bf4CoreTests tests/bf4/TestMain.cpp tests/bf4/Bf4CoreTests.cpp
    tests/bf4/Bf4EmulatedSessionTests.cpp tests/bf4/HolsterTests.cpp tests/bf4/Bf4ModMenuTests.cpp
    tests/bf4/Bf4StereoTests.cpp tests/bf4/VirtualPadTests.cpp tests/bf4/ViewOverrideTests.cpp
    tests/bf4/ReloadClassifierTests.cpp tests/bf4/ReloadDriverTests.cpp)
  target_link_libraries(Bf4CoreTests PRIVATE FvrBf4Core)
  add_test(NAME Bf4Core COMMAND Bf4CoreTests)
endif()

if(NOT (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8))
  return()
endif()

# The existing FvrMinHook target is x86-only (hde32); add the x64 decoder build.
if(NOT TARGET FvrMinHook64)
  add_library(FvrMinHook64 STATIC
    third_party/minhook-1.3.4/src/buffer.c third_party/minhook-1.3.4/src/hook.c
    third_party/minhook-1.3.4/src/trampoline.c third_party/minhook-1.3.4/src/hde/hde64.c)
  target_include_directories(FvrMinHook64 PUBLIC third_party/minhook-1.3.4/include)
endif()

# Read-only driver check for AMD's HD3D quad-buffer stereo extension. Not a test: it
# reports this machine's driver, so it is run by hand, never by CTest.
add_executable(Bf4AmdStereoProbe tools/bf4probe/AmdQbStereoProbe.cpp)
target_compile_definitions(Bf4AmdStereoProbe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

# Session-scoped proxy, copied into the game folder only during a VR session.
add_library(Bf4DxgiProxy SHARED src/platform/windows/bf4/DxgiProxy.cpp src/games/bf4/Bf4SessionInfo.cpp
  src/interaction/ReloadPolicy.cpp src/game/Capability.cpp)
target_include_directories(Bf4DxgiProxy PRIVATE src)
set_target_properties(Bf4DxgiProxy PROPERTIES OUTPUT_NAME dxgi)
target_include_directories(Bf4DxgiProxy PRIVATE include)
target_compile_definitions(Bf4DxgiProxy PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(Bf4DxgiProxy PRIVATE /O1)

# Session-scoped nvapi64.dll: emulated 3D Vision Direct driver (engine stereo route).
add_library(Bf4NvapiShim SHARED src/platform/windows/bf4/NvapiShim.cpp)
set_target_properties(Bf4NvapiShim PROPERTIES OUTPUT_NAME nvapi64)
target_link_libraries(Bf4NvapiShim PRIVATE FvrBf4Core)
target_compile_definitions(Bf4NvapiShim PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# The adapter.
add_library(FrostbiteVR.Bf4 SHARED
  src/games/bf4/Bf4Module.cpp src/games/bf4/Bf4MenuBridge.cpp src/games/bf4/Bf4FrameworkGlue.cpp
  src/games/bf4/Bf4GpuVendor.cpp src/games/bf4/Bf4XInputSink.cpp src/graphics/GdiPanelRaster.cpp
  src/games/bf4/Bf4Diagnostics.cpp)
target_link_libraries(FrostbiteVR.Bf4 PRIVATE FvrBf4Core FvrMinHook64 d3d11 dxgi psapi gdi32)
target_compile_definitions(FrostbiteVR.Bf4 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(FVR_BF4_FRAMEWORK_GLUE)
  target_compile_definitions(FrostbiteVR.Bf4 PRIVATE FVR_BF4_FRAMEWORK_GLUE)
  # World lane: eye crops resampled on the game's device (runtime-compiled shader).
  target_sources(FrostbiteVR.Bf4 PRIVATE src/games/bf4/Bf4EyeResampler.cpp)
  target_link_libraries(FrostbiteVR.Bf4 PRIVATE FvrFrameChannel FvrD3D11FrameBridge d3dcompiler)
endif()

# The XR host for BF4 sessions: the shared OpenXR host (BC2XrHost's main) under the name
# the launcher starts; `--game-exe bf4.exe` selects the watched game process.
add_executable(FvrXrHost src/platform/windows/OpenXrHostMain.cpp)
target_link_libraries(FvrXrHost PRIVATE FvrOpenXR FvrDiagnosticScene FvrFrameChannel BC2BodyAmmoRenderer)
if(EXISTS "${CMAKE_SOURCE_DIR}/runtime/openxr/win64/openxr_loader.dll")
  add_custom_command(TARGET FvrXrHost POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:FvrXrHost>/runtime/openxr/win64"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/runtime/openxr/win64/openxr_loader.dll"
            "$<TARGET_FILE_DIR:FvrXrHost>/runtime/openxr/win64/openxr_loader.dll")
endif()

if(BUILD_TESTING)
  # Fake OpenXR runtime for theater runs: campaign pacing (~18 s), quad layers accepted
  # and sampled. Real GPU, no headset; never selected by the launcher on its own.
  add_library(FvrTheaterTestXr SHARED tests/FakeOpenXrRuntime.cpp)
  target_include_directories(FvrTheaterTestXr PRIVATE third_party/openxr-1.1.61/include)
  target_link_libraries(FvrTheaterTestXr PRIVATE d3d11 dxgi)
  target_compile_definitions(FvrTheaterTestXr PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX FVR_NATIVE_CAMPAIGN_FIXTURE FVR_THEATER_FIXTURE)
endif()

if(BUILD_TESTING AND FVR_BF4_EMU_TEST)
  add_library(Bf4FakePunkBuster SHARED tests/bf4/emu/FakePunkBuster.cpp)
  set_target_properties(Bf4FakePunkBuster PROPERTIES OUTPUT_NAME pbcl)
  add_executable(Bf4EmuHost tests/bf4/emu/Bf4EmuHost.cpp)
  target_compile_definitions(Bf4EmuHost PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  # The emulator also plays the XR host's side of the theater link (menu lane).
  target_link_libraries(Bf4EmuHost PRIVATE d3d11 dxgi delayimp FvrFrameChannel FvrD3D11)
  # Delay-load D3D11 and DXGI: they must bind to the proxy loaded first, as in bf4.exe.
  target_link_options(Bf4EmuHost PRIVATE /DELAYLOAD:d3d11.dll /DELAYLOAD:dxgi.dll)
  add_dependencies(Bf4EmuHost Bf4DxgiProxy Bf4NvapiShim FrostbiteVR.Bf4 Bf4FakePunkBuster)
  # Everything the emulated "game folder" needs sits next to the exe.
  add_custom_command(TARGET Bf4EmuHost POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Bf4DxgiProxy> $<TARGET_FILE_DIR:Bf4EmuHost>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Bf4NvapiShim> $<TARGET_FILE_DIR:Bf4EmuHost>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:FrostbiteVR.Bf4> $<TARGET_FILE_DIR:Bf4EmuHost>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Bf4FakePunkBuster> $<TARGET_FILE_DIR:Bf4EmuHost>)
  add_test(NAME Bf4Emulated COMMAND Bf4EmuHost)
  set_tests_properties(Bf4Emulated PROPERTIES TIMEOUT 60)
endif()
