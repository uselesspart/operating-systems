pkg_check_modules(CRITERION QUIET IMPORTED_TARGET criterion)
if(CRITERION_FOUND)
    add_library(criterion ALIAS PkgConfig::CRITERION)
    return()
endif()

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    message(FATAL_ERROR "Install the Criterion development package for this architecture")
endif()

include(FetchContent)
FetchContent_Declare(criterion_bin
    URL https://github.com/Snaipe/Criterion/releases/download/v2.4.2/criterion-2.4.2-linux-x86_64.tar.xz
    URL_HASH SHA256=f88ef23e2426e705a19699cd606c1283001378827c9b0c6feec0c3912165ac60
)
FetchContent_MakeAvailable(criterion_bin)
add_library(criterion SHARED IMPORTED)
set_target_properties(criterion PROPERTIES
    IMPORTED_LOCATION "${criterion_bin_SOURCE_DIR}/lib/libcriterion.so.3"
    INTERFACE_INCLUDE_DIRECTORIES "${criterion_bin_SOURCE_DIR}/include"
)
