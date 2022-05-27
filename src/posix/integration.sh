# !/bin/bash

set -e
set -x

PROJECT=nshost
OUTDIR=./output
BINARY=${PROJECT}.so

if [ -d ${OUTDIR} ]; then
	rm -fr ${OUTDIR}
fi
mkdir -p output
make clean && make build=debug -j8

VERSION=$(ls ${BINARY}.* | awk -F'so.' '{print $2}')
echo ${VERSION}
TARGET=${BINARY}.${VERSION}

mv ${TARGET} ${OUTDIR}/${TARGET}.Ia64.debug

make clean && make -j8
mv ${TARGET} ${OUTDIR}/${TARGET}.Ia64

make clean && make build=debug arch=arm -j8
mv ${TARGET} ${OUTDIR}/${TARGET}.arm32.debug
make clean && make arch=arm -j8
mv ${TARGET} ${OUTDIR}/${TARGET}.arm32

make clean && make build=debug arch=arm64 -j8
mv ${TARGET} ${OUTDIR}/${TARGET}.aarch64.debug
make clean && make arch=arm64 -j8
mv ${TARGET} ${OUTDIR}/${TARGET}.aarch64

# cleanup all intermediate files
make clean

# record output file checksum
cd ${OUTDIR}
md5sum * > checksum.txt
ls | grep -v checksum.txt | xargs cksum >> checksum.txt
