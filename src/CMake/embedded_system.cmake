# This cmake file is for embedded system. Only support cross compile aarch64
# Custom variables imported by this CMake stub which should be defined by parent CMake:
# XRT_INSTALL_DIR
# XRT_VERSION_MAJOR
# XRT_VERSION_MINOR
# XRT_VERSION_PATCH

INCLUDE (FindPkgConfig)

# DRM
if (NOT DEFINED CROSS_COMPILE)
  pkg_check_modules(DRM REQUIRED libdrm)
  IF(DRM_FOUND)
    MESSAGE(STATUS "Looking for DRM - found at ${DRM_PREFIX} ${DRM_VERSION}")
    INCLUDE_DIRECTORIES(${DRM_INCLUDEDIR})
  ELSE(DRM_FOUND)
    MESSAGE(FATAL_ERROR "Looking for DRM - not found")
  ENDIF(DRM_FOUND)
endif()

# OpenCL header files
find_package(OpenCL)
IF(OPENCL_FOUND)
  MESSAGE(STATUS "Looking for OPENCL - found at ${OPENCL_PREFIX} ${OPENCL_VERSION} ${OPENCL_INCLUDEDIR}")
  INCLUDE_DIRECTORIES(${OPENCL_INCLUDEDIR})
ELSE(OPENCL_FOUND)
  MESSAGE(FATAL_ERROR "Looking for OPENCL - not found")
ENDIF(OPENCL_FOUND)

find_package(Git)

IF(GIT_FOUND)
  message("git found: ${GIT_EXECUTABLE}")
ELSE(GIT_FOUND)
  MESSAGE(FATAL_ERROR "Looking for GIT - not found")
endif(GIT_FOUND)

set(LINUX_FLAVOR ${CMAKE_SYSTEM_NAME})
set(LINUX_KERNEL_VERSION ${CMAKE_SYSTEM_VERSION})

# Support building XRT with local build of Boost libraries. In
# particular the script runtime_src/tools/script/boost.sh downloads
# and builds static Boost libraries compiled with fPIC so that they
# can be used to resolve symbols in XRT dynamic libraries.
if (DEFINED ENV{XRT_BOOST_INSTALL})
  set(XRT_BOOST_INSTALL $ENV{XRT_BOOST_INSTALL})
  set(Boost_USE_STATIC_LIBS ON)
  find_package(Boost
    HINTS $ENV{XRT_BOOST_INSTALL}
    REQUIRED COMPONENTS system filesystem program_options)

  # A bug in FindBoost maybe?  Doesn't set Boost_LIBRARY_DIRS when
  # Boost install has only static libraries. For static tool linking
  # this variable is needed in order for linker to locate the static
  # libraries.  Another bug in FindBoost fails to find static
  # libraries when shared ones are present too.
  if (Boost_FOUND AND "${Boost_LIBRARY_DIRS}" STREQUAL "")
    set (Boost_LIBRARY_DIRS $ENV{XRT_BOOST_INSTALL}/lib)
  endif()

  # Some later versions of boost spews warnings form property_tree
  add_compile_options("-DBOOST_BIND_GLOBAL_PLACEHOLDERS")
else()
  find_package(Boost 
    REQUIRED COMPONENTS system filesystem program_options)
endif()
set(Boost_USE_MULTITHREADED ON)             # Multi-threaded libraries

# Boost_VERSION_STRING is not working properly, use our own macro
set(XRT_BOOST_VERSION ${Boost_MAJOR_VERSION}.${Boost_MINOR_VERSION}.${Boost_SUBMINOR_VERSION})

INCLUDE (FindCurses)
find_package(Curses REQUIRED)

set (XRT_INSTALL_DIR         "/usr")
set (XRT_INSTALL_BIN_DIR     "${XRT_INSTALL_DIR}/bin")
set (XRT_INSTALL_UNWRAPPED_DIR "${XRT_INSTALL_BIN_DIR}/unwrapped")
set (XRT_INSTALL_INCLUDE_DIR "${XRT_INSTALL_DIR}/include/xrt")
set (XRT_INSTALL_LIB_DIR     "${XRT_INSTALL_DIR}/lib${LIB_SUFFIX}")

#Setting RPATH variable for cross compilation
if (DEFINED CROSS_COMPILE)
  set(CMAKE_INSTALL_RPATH "${sysroot}/usr/lib:${sysroot}/lib:${sysroot}/usr/lib/aarch64-linux-gnu")
endif()

# Release OpenCL extension headers
set(XRT_CL_EXT_SRC
  include/1_2/CL/cl_ext_xilinx.h
  include/1_2/CL/cl_ext.h)
install (FILES ${XRT_CL_EXT_SRC} DESTINATION ${XRT_INSTALL_INCLUDE_DIR}/CL)
message("-- XRT CL extension header files")
foreach (header ${XRT_CL_EXT_SRC})
  message("-- ${header}")
endforeach()

add_compile_options("-DXRT_EDGE")

# Let yocto handle license files in the standard way

include (CMake/version.cmake)

include (CMake/ccache.cmake)

message("-- ${CMAKE_SYSTEM_INFO_FILE} (${LINUX_FLAVOR}) (Kernel ${LINUX_KERNEL_VERSION})")
message("-- Compiler: ${CMAKE_CXX_COMPILER} ${CMAKE_C_COMPILER}")

add_subdirectory(runtime_src)

message("-- XRT version: ${XRT_VERSION_STRING}")

message("-- Preparing XRT pkg-config")
set(XRT_PKG_CONFIG_DIR "/usr/lib/pkgconfig")

configure_file (
  ${CMAKE_SOURCE_DIR}/CMake/config/xrt.pc.in
  xrt.pc
  @ONLY
  )
install (
  FILES ${CMAKE_CURRENT_BINARY_DIR}/xrt.pc
  DESTINATION ${XRT_PKG_CONFIG_DIR}
  COMPONENT ${XRT_DEV_COMPONENT}
  )

if (DEFINED CROSS_COMPILE)
  set (LINUX_FLAVOR ${flavor})
  set (LINUX_VERSION ${version})
  include (CMake/cpackLin.cmake)
  set (XRT_DKMS_DRIVER_SRC_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/runtime_src/core")
  include (CMake/dkms-edge.cmake)
endif()
