# Shared native magazine policy tests only. The reviewed fixture is private to
# this test target; no production registry/profile is enabled by this include.
if(BUILD_TESTING)
  add_executable(Bc2SingleFireNativeFamilyTests tests/Bc2SingleFireNativeFamilyTests.cpp
    src/games/bc2/Bc2MagazineNativeRegistry.cpp src/games/bc2/Bc2MagazineReloadCycle.cpp)
  target_include_directories(Bc2SingleFireNativeFamilyTests PRIVATE src/games/bc2 tests)
  target_link_libraries(Bc2SingleFireNativeFamilyTests PRIVATE FvrCore)
  target_compile_definitions(Bc2SingleFireNativeFamilyTests PRIVATE
    FVR_BC2_REVIEWED_MAGAZINE_REGISTRY_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/tests/singlefire243/ReviewedFixture.h")
  if(MSVC)
    set_property(TARGET Bc2SingleFireNativeFamilyTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_test(NAME Bc2SingleFireNativeFamily COMMAND Bc2SingleFireNativeFamilyTests)
endif()
