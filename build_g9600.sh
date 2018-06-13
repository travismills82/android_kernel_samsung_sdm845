#!/bin/bash
#To install OpenSSL development package on Debian, Ubuntu or their derivatives:
#$ sudo apt-get install libssl-dev

export ARCH=arm64
export SEC_BUILD_OPTION_HW_REVISION=02

ROOT_DIR=$(pwd)
OUT_DIR=$ROOT_DIR/out
BUILDING_DIR=$OUT_DIR/kernel_obj

JOB_NUMBER=`grep processor /proc/cpuinfo|wc -l`

CROSS_COMPILER=$ROOT_DIR/lazy-prebuilt/aarch64-linux-android-4.9/bin/aarch64-linux-android- KCFLAGS=-mno-android

DTBTOOL=$ROOT_DIR/tools/dtbTool
DTS_DIR=$BUILDING_DIR/arch/arm64/boot/dts

KERNEL_DIR=$ROOT_DIR/kernel-prebuilt
TEMP_DIR=$OUT_DIR/temp

DEFCONFIG=$1/starqlte_chn_hk_defconfig

FUNC_PRINT()
{
		echo ""
		echo "=============================================="
		echo $1
		echo "=============================================="
		echo ""
}

FUNC_CLEAN()
{
		FUNC_PRINT "Cleaning All"
		rm -rf $OUT_DIR
		mkdir $OUT_DIR
		mkdir -p $BUILDING_DIR
		mkdir -p $TEMP_DIR
}

FUNC_COMPILE_KERNEL()
{
		FUNC_PRINT "Start Compiling Kernel"
		make -C $ROOT_DIR O=$BUILDING_DIR $DEFCONFIG 
		make -C $ROOT_DIR O=$BUILDING_DIR -j$JOB_NUMBER ARCH=arm64 CROSS_COMPILE=$CROSS_COMPILER
		FUNC_PRINT "Finish Compiling Kernel"
}

FUNC_BUILD_DTB()
{
		FUNC_PRINT "Building DTB"
		$DTBTOOL -v -s 2048 -o $TEMP_DIR/dtb $DTS_DIR/
}

FUNC_PACK()
{
		FUNC_PRINT "Start Packing"
		cp -r $KERNEL_DIR/* $TEMP_DIR
		cp $BUILDING_DIR/arch/arm64/boot/Image.gz-dtb $TEMP_DIR/split_img/boot.img-zImage
		mkdir $OUT_DIR/modules
		find . -type f -name "*.ko" | xargs cp -t $OUT_DIR/modules
		cd $TEMP_DIR
		sudo ./repackimg.sh
		cp image-new.img $OUT_DIR/boot.img
		cp $TEMP_DIR/split_img/boot.img-zImage $OUT_DIR/boot.img-zImage
		sudo ./cleanup.sh
		cd $ROOT_DIR
		rm -rf $TEMP_DIR
		rm -rf $BUILDING_DIR
		FUNC_PRINT "Finish Packing"
}

START_TIME=`date +%s`
FUNC_CLEAN
FUNC_COMPILE_KERNEL
FUNC_BUILD_DTB
FUNC_PACK
END_TIME=`date +%s`

let "ELAPSED_TIME=$END_TIME-$START_TIME"
echo "Total compile time is $ELAPSED_TIME seconds"
