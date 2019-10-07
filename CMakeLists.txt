cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
mark_as_advanced(FORCE CMAKE_BUILD_TYPE CMAKE_INSTALL_PREFIX)

project(FEBio2)

##### Set appropriate flag for operating system ####
if(WIN32)
	set(DEF -DWIN32)
elseif(APPLE)
else()
	set(DEF -DLINUX)
endif()

##### Check that paths are valid #####
if(USE_MKL)
	if(WIN32)
		if(NOT EXISTS ${MKL_LIB_DIR}/mkl_intel_lp64.lib)
			message(SEND_ERROR "Could not find mkl_intel_lp64.lib. Check MKL_LIB_DIR.")
		endif()
		if(NOT EXISTS ${MKL_LIB_DIR}/mkl_intel_lp64.lib)
			message(SEND_ERROR "Could not find mkl_core.lib. Check MKL_LIB_DIR.")
		endif()
		if(NOT EXISTS ${MKL_LIB_DIR}/mkl_intel_lp64.lib)
			message(SEND_ERROR "Could not find mkl_intel_thread.lib. Check MKL_LIB_DIR.")
		endif()
		if(NOT EXISTS ${OMP_LIB})
			message(SEND_ERROR "Could not find libiomp5md.lib. Check OMP_LIB.")
		endif()
		if(NOT EXISTS ${MKL_INC}/mkl.h)
			message(SEND_ERROR "Could not find mkl.h. Check MKL_INC.")
		endif()
	else()
		if(NOT DEFINED ENV{MKLROOT})
			message(SEND_ERROR "MKLROOT environment variable not set. Please source compilervars.sh.")
		endif()
	endif()
endif()

if(USE_LEVMAR)
	if(NOT EXISTS ${LEVMAR_INC}/levmar.h)
		message(SEND_ERROR "Could not find levmar.h. Check LEVMAR_INC.")
	endif()
	if(NOT EXISTS ${LEVMAR_LIB})
		if(WIN32)
			message(SEND_ERROR "Could not find levmar.lib. Check LEVMAR_LIB.")
		else()
			message(SEND_ERROR "Could not find liblevmar.a. Check LEVMAR_LIB.")
		endif()
	endif()
endif()

if(USE_ZLIB)
	if(NOT EXISTS ${ZLIB_LIBRARY_RELEASE})
		message(SEND_ERROR "Could not find ZLIB. Check ZLIB_LIBRARY_RELEASE.")
	endif()
	if(NOT EXISTS ${ZLIB_INCLUDE_DIR}/zlib.h)
		message(SEND_ERROR "Could not find zlib.h. Check ZLIB_INCLUDE_DIR.")
	endif()
endif()


##### Search for libaries #####

# MKL - On Unix the compilervars.sh should be run to find the MKL libraries. 
if(WIN32)
	find_path(MKL_INC mkl.h PATH $ENV{ProgramFiles\(x86\)}/IntelSWTools/compilers_and_libraries/windows/mkl/include
		DOC "MKL include directory")
	find_path(MKL_LIB_DIR mkl_core.lib PATH $ENV{ProgramFiles\(x86\)}/IntelSWTools/compilers_and_libraries/windows/mkl/lib/intel64
		DOC "MKL Library directory")
	find_library(OMP_LIB libiomp5md PATH $ENV{ProgramFiles\(x86\)}/IntelSWTools/compilers_and_libraries/windows/compiler/lib/intel64
		DOC "OMP Library")		
	
	if(MKL_INC AND MKL_LIB_DIR AND OMP_LIB)
		option(USE_MKL "Required for pardiso and iterative solvers" ON)
	else()
		option(USE_MKL "Required for pardiso and iterative solvers" OFF)
	endif()
else()
	if(DEFINED ENV{MKLROOT})
		option(USE_MKL "Required for pardiso and iterative solvers" ON)
	else()
		option(USE_MKL "Required for pardiso and iterative solvers" OFF)
	endif()
endif()

# LEVMAR
if(WIN32)
	find_path(LEVMAR_INC levmar.h PATH C::/Program\ Files/* $ENV{HOMEPATH}/* $ENV{HOMEPATH}/*/*
		DOC "Levmar include directory")
	find_library(LEVMAR_LIB levmar PATH C::/Program\ Files/* $ENV{HOMEPATH}/* $ENV{HOMEPATH}/*/*
		DOC "Levmar library path")
else()
	find_path(LEVMAR_INC levmar.h PATH /usr/local/ /opt/levmar* $ENV{HOME}/* $ENV{HOME}/*/*
		DOC "Levmar include directory")
	find_library(LEVMAR_LIB levmar PATH /usr/local/ /opt/levmar* $ENV{HOME}/* $ENV{HOME}/*/*
		DOC "Levmar library path")
endif()	

if(LEVMAR_INC AND LEVMAR_LIB)		
	option(USE_LEVMAR "Required for optimization in FEBio" ON)
else()
	option(USE_LEVMAR "Required for optimization in FEBio" OFF)
endif()

# ZLIB
include(FindZLIB)
mark_as_advanced(CLEAR ZLIB_INCLUDE_DIR ZLIB_LIBRARY_RELEASE)

if(ZLIB_INCLUDE_DIR AND ZLIB_LIBRARY_RELEASE)		
	option(USE_ZLIB "Required for compressing xplt files" ON)
else()
	option(USE_ZLIB "Required for compressing xplt files" OFF)
endif()


##### Set appropriate defines and includes #####
set(INC ${PROJECT_SOURCE_DIR})

if(USE_MKL)
	set(DEF ${DEF} -DMKL_ISS)
	set(DEF ${DEF} -DPARDISO)
	set(INC ${INC} ${MKL_INC})
endif(USE_MKL)

if(USE_LEVMAR)
	set(DEF ${DEF} -DHAVE_LEVMAR)
	set(INC ${INC} ${LEVMAR_INC})
endif(USE_LEVMAR)

if(USE_ZLIB)
	set(DEF ${DEF} -DHAVE_ZLIB)
	set(INC ${INC} ${ZLIB_INCLUDE_DIR})
endif(USE_ZLIB)

set(DEF ${DEF} -DNDEBUG)

add_definitions(${DEF})
include_directories(${INC})

##### Include FEBio libraries #####
add_subdirectory (FEBioFluid)
add_subdirectory (FEBioLib)
add_subdirectory (FEBioMech)
add_subdirectory (FEBioMix)
add_subdirectory (FEBioOpt)
add_subdirectory (FEBioPlot)
add_subdirectory (FEBioTest)
add_subdirectory (FEBioXML)
add_subdirectory (FECore)
add_subdirectory (NumCore)

##### Set up compiler flags #####
if(WIN32)
elseif(APPLE)
	add_definitions(-O3 -fPIC -std=c++11 -static-libstdc++ -w)
else()
	add_definitions(-O3 -fPIC -fopenmp -std=c++11 -static-libstdc++ -w)
endif()

# Extra compiler flags for intel compiler
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Intel")
	add_definitions(-static-intel -no-intel-extensions)
endif()

##### Set up executable compilation #####
file(GLOB SOURCES "FEBio2/*.cpp")
add_executable (febio2 ${SOURCES})

##### Linking options #####

# Link FEBio libraries
if(WIN32 OR APPLE)
	target_link_libraries(febio2 fecore febiolib febioplot febiomech 
		febiomix febioxml numcore febioopt febiotest febiofluid)
else()
	target_link_libraries(febio2 -Wl,--start-group fecore febiolib febioplot febiomech 
		febiomix febioxml numcore febioopt febiotest febiofluid -Wl,--end-group)
endif()

# Link LEVMAR
if(USE_LEVMAR)
	target_link_libraries(febio2 ${LEVMAR_LIB})
endif(USE_LEVMAR)

# Link MKL
if(USE_MKL)
	if(WIN32)
		target_link_libraries(febio2 ${MKL_LIB_DIR}/mkl_intel_lp64.lib ${MKL_LIB_DIR}/mkl_core.lib
			${MKL_LIB_DIR}/mkl_intel_thread.lib)
		target_link_libraries(febio2 ${OMP_LIB})
	elseif(APPLE)
		target_link_libraries(febio2 $ENV{MKLROOT}/lib/libmkl_intel_lp64.a $ENV{MKLROOT}/lib/libmkl_core.a
			$ENV{MKLROOT}/lib/libmkl_intel_thread.a)
		target_link_libraries(febio2 -liomp5 -pthread -ldl)
	else()
		target_link_libraries(febio2 -Wl,--start-group $ENV{MKLROOT}/lib/intel64/libmkl_intel_lp64.a $ENV{MKLROOT}/lib/intel64/libmkl_core.a
			$ENV{MKLROOT}/lib/intel64/libmkl_intel_thread.a -Wl,--end-group)
		target_link_libraries(febio2 -liomp5 -pthread -ldl)
	endif()
endif()

# Link ZLIB
if(USE_ZLIB)
	target_link_libraries(febio2 ${ZLIB_LIBRARY_RELEASE})
endif(USE_ZLIB)


##### Create febio.xml #####
if(USE_MKL)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/febio.xml "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>
<febio_config version=\"1.0\">
	<linear_solver type=\"pardiso\"></linear_solver>
</febio_config>
")
else()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/febio.xml "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>
<febio_config version=\"1.0\">
	<linear_solver type=\"skyline\"></linear_solver>
</febio_config>
")
endif()


