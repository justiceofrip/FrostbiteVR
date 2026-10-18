# Exact data joins use ordinary consumers. Tests supply synthetic native/pack
# boundaries only; no fixture becomes a second gameplay backend.
if(BUILD_TESTING AND TARGET BC2BodyAmmoRenderer)
  add_executable(Bc2ResourceCarriedEnrollmentTests tests/ResourceCarried233Tests.cpp)
  target_include_directories(Bc2ResourceCarriedEnrollmentTests PRIVATE tests src/platform/windows)
  target_link_libraries(Bc2ResourceCarriedEnrollmentTests PRIVATE BC2BodyAmmoRenderer)
  add_test(NAME Bc2ResourceCarriedEnrollment COMMAND Bc2ResourceCarriedEnrollmentTests)
  # Production admission requires an independently source-bound operation
  # review. Default builds continue to prove rejection without this capability.
  if(BC2_NATIVE_OPERATION_CAPABILITIES_HEADER)
    add_executable(Bc2ResourceEnrollmentTests tests/ResourceEnrollment233Tests.cpp
      src/games/bc2/Bc2MagazineNativeRegistry.cpp src/games/bc2/Bc2MagazineGeometryProfile.cpp)
    target_include_directories(Bc2ResourceEnrollmentTests PRIVATE tests src/platform/windows)
    target_link_libraries(Bc2ResourceEnrollmentTests PRIVATE BC2BodyAmmoRenderer)
    # Match the committed complete body/visibility catalog, including SP_01.
    # Isolated older-family regressions below keep their own reviewed headers.
    target_compile_definitions(Bc2ResourceEnrollmentTests PRIVATE
      FVR_BC2_REVIEWED_MAGAZINE_REGISTRY_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/profiles/opening426/CombinedRegistry.h"
      FVR_BC2_EXPERIMENTAL_MAGAZINE_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/profiles/opening426/CombinedGeometry.h")
    add_test(NAME Bc2ResourceEnrollment COMMAND Bc2ResourceEnrollmentTests)
  endif()
endif()

if(BUILD_TESTING AND TARGET BC2BodyAmmoRenderer)
  add_executable(Bc2F2000AssemblyEnrollmentTests tests/Bc2F2000AssemblyEnrollmentTests.cpp
    src/games/bc2/Bc2MagazineNativeRegistry.cpp src/games/bc2/Bc2MagazineGeometryProfile.cpp)
  target_include_directories(Bc2F2000AssemblyEnrollmentTests PRIVATE tests src/platform/windows)
  target_link_libraries(Bc2F2000AssemblyEnrollmentTests PRIVATE BC2BodyAmmoRenderer)
  target_compile_definitions(Bc2F2000AssemblyEnrollmentTests PRIVATE
    FVR_BC2_REVIEWED_MAGAZINE_REGISTRY_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/profiles/resource-enrollment295-pistol/CombinedRegistry.h"
    FVR_BC2_EXPERIMENTAL_MAGAZINE_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/profiles/resource-enrollment295-pistol/CombinedGeometry.h")
  add_test(NAME Bc2F2000AssemblyEnrollment COMMAND Bc2F2000AssemblyEnrollmentTests)
endif()

# Run the same real resource consumer cases for all 15 exact variants, including the rigid magazine assembly.
if(BUILD_TESTING)
  add_executable(Bc2ResourceScarConsumerTests tests/resource-batch227/ResourceBatchTests.cpp
    src/games/bc2/Bc2MagazineNativeRegistry.cpp src/games/bc2/Bc2MagazineGeometryProfile.cpp)
  target_include_directories(Bc2ResourceScarConsumerTests PRIVATE tests src/platform/windows)
  target_link_libraries(Bc2ResourceScarConsumerTests PRIVATE BC2Camera FvrCore)
  target_compile_definitions(Bc2ResourceScarConsumerTests PRIVATE FVR_EXPECTED_RESOURCE_PROFILE_COUNT=16
    FVR_BC2_REVIEWED_MAGAZINE_REGISTRY_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/profiles/resource-enrollment295-pistol/CombinedRegistry.h"
    FVR_BC2_EXPERIMENTAL_MAGAZINE_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/profiles/resource-enrollment295-pistol/CombinedGeometry.h")
  add_test(NAME Bc2ResourceScarConsumer COMMAND Bc2ResourceScarConsumerTests)
endif()
