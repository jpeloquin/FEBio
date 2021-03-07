#!/usr/bin/env bash
set -v

if [ $# == 0 ]; then
	echo "Usage: Mkdir.bash platform"
	exit
fi

mkdir -p "$1"
cd "$1"
mkdir -p FEBio3
mkdir -p FEBioLib
mkdir -p FEBioMech
mkdir -p FEBioMix
mkdir -p FEBioOpt
mkdir -p FEBioPlot
mkdir -p FEBioTest
mkdir -p FEBioFluid
mkdir -p FEBioXML
mkdir -p FECore
mkdir -p NumCore
