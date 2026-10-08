# Offline descriptor/chamber boundary only. No native runtime enrollment.
if(BUILD_TESTING)
  add_executable(Bc2SingleFireCandidateTests tests/SingleFireCandidateTests.cpp
    src/games/bc2/Bc2MagazineNativeRegistry.cpp)
  target_include_directories(Bc2SingleFireCandidateTests PRIVATE src/games/bc2 profiles/singlefire238)
  target_link_libraries(Bc2SingleFireCandidateTests PRIVATE FvrCore)
  target_compile_definitions(Bc2SingleFireCandidateTests PRIVATE
    FVR_BC2_REVIEWED_MAGAZINE_REGISTRY_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/profiles/singlefire238/DisabledRegistry.h")
  if(MSVC)
    set_property(TARGET Bc2SingleFireCandidateTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_test(NAME Bc2SingleFireCandidate COMMAND Bc2SingleFireCandidateTests)
endif()
