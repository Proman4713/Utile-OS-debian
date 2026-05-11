#!/bin/bash

set -e

EXTENSION_URL=$1
EXTENSION_NAME=$2
OUTPUT_DIR=$3

if ! [ -d "$OUTPUT_DIR" ]; then
	mkdir -p "$OUTPUT_DIR"
fi

# Validate extension name and url
if ! [ -n "$EXTENSION_NAME" ]; then
	echo "Missing extension name"
	exit 1
fi

if ! [ -n "$EXTENSION_URL" ]; then
	echo "Missing extension url"
	exit 1
fi

if ! [ -d "$OUTPUT_DIR/$EXTENSION_NAME" ]; then
	mkdir -p "$OUTPUT_DIR/$EXTENSION_NAME"
fi

wget -qO "$OUTPUT_DIR/$EXTENSION_NAME.zip" "$EXTENSION_URL"
unzip -qo "$OUTPUT_DIR/$EXTENSION_NAME.zip" -d "$OUTPUT_DIR/$EXTENSION_NAME"

if [ -d "$OUTPUT_DIR/$EXTENSION_NAME/schemas" ]; then
	sudo glib-compile-schemas $OUTPUT_DIR/$EXTENSION_NAME/schemas
	echo -e "\nRegistering settings system-wide for $EXTENSION_NAME...\n"
	cp "$OUTPUT_DIR/$EXTENSION_NAME/schemas/"*.xml $OUTPUT_DIR/../../glib-2.0/schemas/
fi

rm -f "$OUTPUT_DIR/$EXTENSION_NAME.zip"