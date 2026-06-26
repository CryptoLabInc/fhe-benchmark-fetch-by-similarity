include(CMakeFindDependencyMacro)
set(juvia_VERSION 0.1.0)


####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was Config.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

# juvia's public interface links CUDA::cudart; ensure the imported target
# exists for consumers of the installed package.
find_dependency(CUDAToolkit)

# Relocatable: resolved relative to the install prefix at find_package() time
# (PACKAGE_INCLUDE_INSTALL_DIR comes from PATH_VARS in configure_package_config_file).
set_and_check(juvia_INCLUDE_DIR "${PACKAGE_PREFIX_DIR}/include")

include("${CMAKE_CURRENT_LIST_DIR}/juviaTargets.cmake")

check_required_components(juvia)
