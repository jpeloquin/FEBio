# Make include file for FEBio on Linux 64 bit

CC = icpc  # tested with icpc 2021.1 Beta 20201112

DEF = -DLINUX -DMKL_ISS -DPARDISO -DHAVE_ZLIB -DSVN

FLG = -g -O3 -fPIC -fopenmp -qopenmp-link=static -static-intel -no-intel-extensions -std=c++11 -static-libstdc++

# Intel Compiler
INTELROOT = $(subst /mkl,/compiler,$(MKLROOT))/linux/compiler
INTEL_LIB = $(INTELROOT)/lib/intel64

# Pardiso solver
MKL_PATH = $(MKLROOT)/lib/intel64
MKL_INC = $(MKLROOT)/include
MKL_LIB = -Wl,--start-group $(MKL_PATH)/libmkl_intel_lp64.a
MKL_LIB += $(MKL_PATH)/libmkl_core.a $(MKL_PATH)/libmkl_intel_thread.a -Wl,--end-group
MKL_LIB += $(INTEL_LIB)/libiomp5.a -pthread -lz

# Levmar library
#
# The levmar library is expected to be at build/lib/liblevmar.a and the
# header at build/include/levmar.h.  To not use the Lourakis levmar
# routine, comment out the following 2 lines.
DEF += -DHAVE_LEVMAR
LEV_LIB = -llevmar

# GSL library
#
# The GSL libraries are expected to be in build/lib and the GSL headers
# are expected to be in build/include/gsl.  To not use the GNU
# Scientific Library, comment out the following 2 lines.
DEF += -DHAVE_GSL
GSL_LIB = -lgsl

# SuperLU library
SUPERLU_INC = /home/sci/rawlins/Projects/SuperLU/4.3/SRC/
SUPERLU_LIB =	-lsuperlu_4.3

# SuperLU_MT library
SUPERLUMT_INC = /home/sci/rawlins/Projects/SuperLU_MT_2.2/SRC/
SUPERLUMT_LIB =	-lsuperlu_mt_OPENMP

LIBS = -L$(FEBDIR)/build/lib $(LEV_LIB) $(MKL_LIB) $(GSL_LIB)

INC = -I$(MKL_INC) -I$(FEBDIR) -I$(FEBDIR)build/include
