# Make include file for FEBio on Linux

include $(FEBDIR)build/lnx64.mk

MKL_INC = $(MKLROOT)/include

CC = g++

FLG = -O3 -fPIC -fopenmp -std=c++11

# Pardiso solver
#
# When compiling with the Intel Performance Libraries alone (no Intel
# C++ Compiler / icpc), MKLROOT should look something like
# `/opt/intel/compilers_and_libraries_2020.0.166/linux/mkl/`.  The
# `compiler` directory must exist as a sibling of `mkl` in the given
# path; `/opt/intel/mkl` fails.
MKL_PATH = $(MKLROOT)/lib/intel64
MKL_LIB = -Wl,--start-group $(MKL_PATH)/libmkl_intel_lp64.a
MKL_LIB += $(MKL_PATH)/libmkl_core.a $(MKL_PATH)/libmkl_intel_thread.a -Wl,--end-group
MKL_LIB += $(INTEL_LIB)/libiomp5.a -pthread -lz

LIBS = -L$(FEBDIR)/build/lib $(LEV_LIB) $(MKL_LIB) $(GSL_LIB) '-Wl,-rpath,$$ORIGIN'

INC = -I$(FEBDIR) -I$(FEBDIR)build/include -I$(LEV_INC) -I$(MKL_INC)
