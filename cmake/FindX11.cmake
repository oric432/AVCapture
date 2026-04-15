# FindX11.cmake - Custom X11 finder for static builds
# ---------------------------------------------------
# Imported Targets:
#   X11::X11, X11::Xext, X11::Xfixes, X11::Xrender, X11::Xrandr, X11::Xau, X11::Xdmcp, X11::xcb

# Allow user to specify custom X11 installation prefix
set(X11_ROOT "/usr/local" CACHE PATH "X11 installation prefix")

# Helper function to find a library and create imported target
function(find_x11_library libname header_subpath)
    string(TOUPPER ${libname} LIBNAME_UPPER)
    
    # Find include directory
    find_path(${LIBNAME_UPPER}_INCLUDE_DIR
        NAMES ${header_subpath}
        HINTS
            ${X11_ROOT}/include
            /usr/local/include
            /usr/include
        PATH_SUFFIXES
            X11
            xcb
    )
    
    # Find the static library first, then fallback to shared
    find_library(${LIBNAME_UPPER}_LIBRARY
        NAMES 
            lib${libname}.a      # Static library (preferred)
            ${libname}           # Shared library (fallback)
        HINTS
            ${X11_ROOT}/lib
            ${X11_ROOT}/lib64
            /usr/local/lib
            /usr/local/lib64
            /usr/lib
            /usr/lib64
        NO_DEFAULT_PATH
    )
    
    # Fallback to default search if not found
    if(NOT ${LIBNAME_UPPER}_LIBRARY)
        find_library(${LIBNAME_UPPER}_LIBRARY
            NAMES lib${libname}.a ${libname}
        )
    endif()
    
    # Mark as found if both include and library exist
    if(${LIBNAME_UPPER}_INCLUDE_DIR AND ${LIBNAME_UPPER}_LIBRARY)
        set(${LIBNAME_UPPER}_FOUND TRUE PARENT_SCOPE)
        
        # Create imported target if it doesn't exist
        if(NOT TARGET X11::${libname})
            # Determine if it's a static library
            get_filename_component(lib_ext ${${LIBNAME_UPPER}_LIBRARY} EXT)
            if(lib_ext STREQUAL ".a")
                add_library(X11::${libname} STATIC IMPORTED)
            else()
                add_library(X11::${libname} UNKNOWN IMPORTED)
            endif()
            
            set_target_properties(X11::${libname} PROPERTIES
                IMPORTED_LOCATION "${${LIBNAME_UPPER}_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${${LIBNAME_UPPER}_INCLUDE_DIR}"
            )
            
            # For static libraries, ensure position independent code
            if(lib_ext STREQUAL ".a")
                set_target_properties(X11::${libname} PROPERTIES
                    INTERFACE_COMPILE_OPTIONS "-fPIC"
                )
            endif()
        endif()
        
        # Export to parent scope
        set(${LIBNAME_UPPER}_INCLUDE_DIR ${${LIBNAME_UPPER}_INCLUDE_DIR} PARENT_SCOPE)
        set(${LIBNAME_UPPER}_LIBRARY ${${LIBNAME_UPPER}_LIBRARY} PARENT_SCOPE)
        
        mark_as_advanced(${LIBNAME_UPPER}_INCLUDE_DIR ${LIBNAME_UPPER}_LIBRARY)
    else()
        set(${LIBNAME_UPPER}_FOUND FALSE PARENT_SCOPE)
    endif()
endfunction()

# Find all X11 components in dependency order
# Base libraries first
find_x11_library(Xau "X11/Xauth.h")
find_x11_library(Xdmcp "X11/Xdmcp.h")
find_x11_library(xcb "xcb/xcb.h")

# Core X11
find_x11_library(X11 "X11/Xlib.h")

# Extensions (in dependency order)
find_x11_library(Xext "X11/extensions/Xext.h")
find_x11_library(Xrender "X11/extensions/Xrender.h")
find_x11_library(Xrandr "X11/extensions/Xrandr.h")
find_x11_library(Xfixes "X11/extensions/Xfixes.h")

# Set up dependencies between libraries
if(TARGET X11::X11)
    # libX11 depends on libxcb, libXau, libXdmcp
    set(X11_DEPENDENCIES)
    if(TARGET X11::xcb)
        list(APPEND X11_DEPENDENCIES X11::xcb)
    endif()
    if(TARGET X11::Xau)
        list(APPEND X11_DEPENDENCIES X11::Xau)
    endif()
    if(TARGET X11::Xdmcp)
        list(APPEND X11_DEPENDENCIES X11::Xdmcp)
    endif()
    
    if(X11_DEPENDENCIES)
        set_target_properties(X11::X11 PROPERTIES
            INTERFACE_LINK_LIBRARIES "${X11_DEPENDENCIES}"
        )
    endif()
endif()

if(TARGET X11::Xext)
    # libXext depends on libX11
    if(TARGET X11::X11)
        set_target_properties(X11::Xext PROPERTIES
            INTERFACE_LINK_LIBRARIES "X11::X11"
        )
    endif()
endif()

if(TARGET X11::Xrender)
    # libXrender depends on libX11
    if(TARGET X11::X11)
        set_target_properties(X11::Xrender PROPERTIES
            INTERFACE_LINK_LIBRARIES "X11::X11"
        )
    endif()
endif()

if(TARGET X11::Xrandr)
    # libXrandr depends on libX11, libXext, and libXrender
    set(XRANDR_DEPENDENCIES)
    if(TARGET X11::X11)
        list(APPEND XRANDR_DEPENDENCIES X11::X11)
    endif()
    if(TARGET X11::Xext)
        list(APPEND XRANDR_DEPENDENCIES X11::Xext)
    endif()
    if(TARGET X11::Xrender)
        list(APPEND XRANDR_DEPENDENCIES X11::Xrender)
    endif()
    
    if(XRANDR_DEPENDENCIES)
        set_target_properties(X11::Xrandr PROPERTIES
            INTERFACE_LINK_LIBRARIES "${XRANDR_DEPENDENCIES}"
        )
    endif()
endif()

if(TARGET X11::Xfixes)
    # libXfixes depends on libX11
    if(TARGET X11::X11)
        set_target_properties(X11::Xfixes PROPERTIES
            INTERFACE_LINK_LIBRARIES "X11::X11"
        )
    endif()
endif()

# Collect all include directories
set(X11_INCLUDE_DIRS)
foreach(lib X11 Xext Xrender Xrandr Xfixes Xau Xdmcp xcb)
    string(TOUPPER ${lib} LIB_UPPER)
    if(${LIB_UPPER}_INCLUDE_DIR)
        list(APPEND X11_INCLUDE_DIRS ${${LIB_UPPER}_INCLUDE_DIR})
    endif()
endforeach()
if(X11_INCLUDE_DIRS)
    list(REMOVE_DUPLICATES X11_INCLUDE_DIRS)
endif()

# Collect all libraries
set(X11_LIBRARIES)
foreach(lib X11 Xext Xrender Xrandr Xfixes Xau Xdmcp xcb)
    string(TOUPPER ${lib} LIB_UPPER)
    if(${LIB_UPPER}_LIBRARY)
        list(APPEND X11_LIBRARIES ${${LIB_UPPER}_LIBRARY})
    endif()
endforeach()

# Handle component requirements
set(X11_FOUND TRUE)
if(X11_FIND_COMPONENTS)
    foreach(component ${X11_FIND_COMPONENTS})
        string(TOUPPER ${component} COMPONENT_UPPER)
        if(NOT ${COMPONENT_UPPER}_FOUND)
            set(X11_FOUND FALSE)
            if(X11_FIND_REQUIRED_${component})
                message(FATAL_ERROR "X11 component ${component} not found")
            endif()
        endif()
    endforeach()
else()
    # If no components specified, require at least X11 core library
    if(NOT X11_FOUND)
        set(X11_FOUND FALSE)
    endif()
endif()

# Standard CMake package handling
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(X11
    REQUIRED_VARS X11_LIBRARIES X11_INCLUDE_DIRS
    FOUND_VAR X11_FOUND
)

# Print what we found (helpful for debugging)
if(X11_FOUND AND NOT X11_FIND_QUIETLY)
    message(STATUS "Found X11:")
    message(STATUS "  Include dirs: ${X11_INCLUDE_DIRS}")
    message(STATUS "  Libraries:")
    foreach(lib X11 Xext Xrender Xrandr Xfixes Xau Xdmcp xcb)
        string(TOUPPER ${lib} LIB_UPPER)
        if(TARGET X11::${lib})
            message(STATUS "    - X11::${lib}: ${${LIB_UPPER}_LIBRARY}")
        endif()
    endforeach()
endif()