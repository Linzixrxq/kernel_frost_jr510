#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2020, The Linux Foundation. All rights reserved.

# Script to generate a defconfig variant based on the input

set -x

usage() {
	echo "Usage: $0 <platform_defconfig_variant>"
	echo "Variants: <platform>-gki_defconfig, <platform>-qgki_defconfig, <platform>-consolidate_defconfig and <platform>-qgki-debug_defconfig"
	echo "Example: $0 lahaina-gki_defconfig"
	exit 1
}

if [ -z "$1" ]; then
	echo "Error: Failed to pass input argument"
	usage
fi

SCRIPTS_ROOT=$(readlink -f $(dirname $0)/)

TEMP_DEF_NAME=`echo $1 | sed -r "s/_defconfig$//"`
DEF_VARIANT=`echo ${TEMP_DEF_NAME} | sed -r "s/.*-//"`
PLATFORM_NAME=`echo ${TEMP_DEF_NAME} | sed -r "s/-.*$//"`

PLATFORM_NAME=`echo $PLATFORM_NAME | sed "s/vendor\///g"`

REQUIRED_DEFCONFIG=`echo $1 | sed "s/vendor\///g"`

# We should be in the kernel root after the envsetup
# if [[  "${REQUIRED_DEFCONFIG}" != *"gki"* ]]; then
#	source ${SCRIPTS_ROOT}/envsetup.sh $PLATFORM_NAME generic_defconfig
# else
	source ${SCRIPTS_ROOT}/envsetup.sh $PLATFORM_NAME
# fi

FINAL_DEFCONFIG_BLEND=""

case "$REQUIRED_DEFCONFIG" in
	${PLATFORM_NAME}-jgki-debug_defconfig )
		FINAL_DEFCONFIG_BLEND+=" $QCOM_DEBUG_FRAG"
		;;
	${PLATFORM_NAME}-jgki-perfuser_defconfig )
		FINAL_DEFCONFIG_BLEND+=" ${CONFIGS_DIR}/${PLATFORM_NAME}_JGKI-perfuser.config "
		;&	# Intentional fallthrough
	${PLATFORM_NAME}-jgki_defconfig )
		FINAL_DEFCONFIG_BLEND+=" $QCOM_DEBUG_FS_FRAG "
		FINAL_DEFCONFIG_BLEND+=" $QCOM_JGKI_FRAG "
		;;
	${PLATFORM_NAME}-gki_defconfig )
		FINAL_DEFCONFIG_BLEND+=" $QCOM_GKI_FRAG "
		;;
esac

# Reverse the order of the configs for the override to work properly
# Correct order is base_defconfig GKI.config QGKI.config consolidate.config debug.config
FINAL_DEFCONFIG_BLEND=`echo "${FINAL_DEFCONFIG_BLEND}" | awk '{ for (i=NF; i>1; i--) printf("%s ",$i); print $1; }'`

echo "defconfig blend for $REQUIRED_DEFCONFIG: ${BASE_DEFCONFIG} $FINAL_DEFCONFIG_BLEND"

cat ${BASE_DEFCONFIG} > ${CONFIGS_DIR}/$REQUIRED_DEFCONFIG
for ORIG_MERGE_FILE in ${FINAL_DEFCONFIG_BLEND} ; do
	cat ${ORIG_MERGE_FILE} >> ${CONFIGS_DIR}/$REQUIRED_DEFCONFIG
done
