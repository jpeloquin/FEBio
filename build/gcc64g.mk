# Make include file for Linux GCC 64-bit debug build

include $(FEBDIR)build/gcc64.mk

FLG := $(FLG:O3=g)
