# Coordinator applies this only when the complete weapon batch is ready.
include("${CMAKE_CURRENT_LIST_DIR}/../mechanism-family41/Enrollment.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../weapons45-magazines/Enrollment.cmake")
set(BC2_AUTHORED_SUPPORT_HEADER "${CMAKE_CURRENT_LIST_DIR}/CombinedSupport.h" CACHE FILEPATH "Combined firearm support contacts" FORCE)
set(BC2_SHOTGUN_EQUIPMENT_HEADER "${CMAKE_CURRENT_LIST_DIR}/ShotgunEquipment.generated.h" CACHE FILEPATH "Exact shotgun equipment profiles" FORCE)
set(BC2_BOLT_EQUIPMENT_HEADER "${CMAKE_CURRENT_LIST_DIR}/BoltEquipment.generated.h" CACHE FILEPATH "Exact bolt equipment profiles" FORCE)
