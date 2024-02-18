# Set the module name and version
set(Vector_BLF "Vector_BLF")
set(Vector_BLF_Version "2.4.1")

# Set the default search paths
set(Vector_BLF_INCLUDE_DIRS "/usr/include" "/usr/local/include")
set(Vector_BLF_LIBRARY_DIRS "/usr/lib" "/usr/local/lib")

# Check if the library exists and set related variables
find_path(Vector_BLF_INCLUDE_DIR NAMES Vector PATHS ${Vector_BLF_INCLUDE_DIRS})
find_library(Vector_BLF_LIBRARY NAMES Vector_BLF PATHS ${Vector_BLF_LIBRARY_DIRS})

# Check if the results are valid and report error messages
if(NOT Vector_BLF_INCLUDE_DIR)
    message(FATAL_ERROR "Cannot find Vector")
endif()

if(NOT Vector_BLF_LIBRARY)
    message(FATAL_ERROR "Cannot find ${Vector_BLF} library")
endif()

# Set the related variable
set(Vector_BLF_FOUND TRUE)
set(Vector_BLF_INCLUDE_DIRS ${Vector_BLF_INCLUDE_DIR})
set(Vector_BLF_LIBRARIES ${Vector_BLF_LIBRARY})

message(STATUS "Found ${Vector_BLFName}: ${Vector_BLF_INCLUDE_DIRS}, ${Vector_BLF_LIBRARIES}")
