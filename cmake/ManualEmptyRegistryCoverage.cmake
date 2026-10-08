# Optional offline validation only. Frozen private headers are not release payload.
set(BC2_MANUAL_EMPTY_COVERAGE_FIXTURE_DIR "" CACHE PATH "Offline 21-row registry fixtures; never game registration")
if(BC2_MANUAL_EMPTY_COVERAGE_FIXTURE_DIR)
  foreach(_mode IN ITEMS Disabled Fixture)
    if(NOT EXISTS "${BC2_MANUAL_EMPTY_COVERAGE_FIXTURE_DIR}/${_mode}Registry.h")
      message(FATAL_ERROR "Missing actual frozen ${_mode}Registry.h")
    endif()
    set(_target "Bc2ManualEmptyRegistryCoverage${_mode}Tests")
    add_executable(${_target}
      tests/Bc2ManualEmptyRegistryCoverageTests.cpp
      src/games/bc2/Bc2MagazineNativeRegistry.cpp
      src/games/bc2/Bc2ReloadRoundGate.cpp
      src/games/bc2/Bc2ReloadHold.cpp)
    target_include_directories(${_target} PRIVATE include tests src/games/bc2 "${BC2_MANUAL_EMPTY_COVERAGE_FIXTURE_DIR}")
    target_compile_definitions(${_target} PRIVATE NDEBUG NOMINMAX FVR_BC2_REVIEWED_MAGAZINE_REGISTRY_HEADER="${_mode}Registry.h")
    if(_mode STREQUAL "Disabled")
      target_compile_definitions(${_target} PRIVATE COVERAGE_DISABLED)
    endif()
    if(MSVC)
      target_compile_options(${_target} PRIVATE /W4 /WX)
    endif()
    add_test(NAME "Bc2ManualEmptyRegistryCoverage${_mode}" COMMAND ${_target})
  endforeach()
endif()
