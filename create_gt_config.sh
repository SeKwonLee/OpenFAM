#!/bin/bash
CURRENTDIR=`pwd`
BUILD_DIR=$CURRENTDIR/build
INSTALL_DIR=$BUILD_DIR
TOOL_DIR=$CURRENTDIR/tools
CONFIG_OUT_DIR=$BUILD_DIR/config

$BUILD_DIR/bin/openfam_adm @$TOOL_DIR/gt-config.txt --create_config_files --config_file_path $CONFIG_OUT_DIR --install_path $INSTALL_DIR
