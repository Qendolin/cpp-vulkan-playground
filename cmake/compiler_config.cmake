
message("Using compiler '${CMAKE_CXX_COMPILER}'")
if (CMAKE_CXX_COMPILER MATCHES "clang") # can't use CMAKE_CXX_COMPILER_ID before project() but VCPKG_CXX_FLAGS need to be set before it :confused:
    #    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -std=c++23")
    #    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++ -L/usr/lib/llvm-19/lib/ -lc++ -lc++abi")
    #    set(VCPKG_CXX_FLAGS "${VCPKG_CXX_FLAGS} ${CMAKE_CXX_FLAGS}")
    #    set(VCPKG_LINKER_FLAGS "${VCPKG_LINKER_FLAGS} ${CMAKE_EXE_LINKER_FLAGS}")
endif ()

function(set_compiler_warnings target)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE /W4)
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(${target} PRIVATE -Wall -Wpedantic)
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE -Wall -Wpedantic)
    endif ()
endfunction()

function(set_compiler_flags target)
    set_compiler_warnings(${target})

    if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE /std:c++23preview)
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    endif ()
endfunction()