# Apply to the canonical build trees only after integration is authorized.
# This file configures source tables; it does not install the separately staged cache.
set(BC2_FEED_REGISTRY_HEADER "${CMAKE_CURRENT_LIST_DIR}/Feed.generated.h" CACHE FILEPATH "Reviewed feed family" FORCE)
set(BC2_PISTOL_SLIDE_REGISTRY_HEADER "${CMAKE_CURRENT_LIST_DIR}/Slide.generated.h" CACHE FILEPATH "Reviewed slide family" FORCE)
set(BC2_REVIEWED_MAGAZINE_REGISTRY_HEADER "${CMAKE_CURRENT_LIST_DIR}/CombinedRegistry.h" CACHE FILEPATH "Combined magazine native registrations" FORCE)
set(BC2_EXPERIMENTAL_MAGAZINE_HEADER "${CMAKE_CURRENT_LIST_DIR}/CombinedGeometry.h" CACHE FILEPATH "Combined authored magazine geometry" FORCE)
