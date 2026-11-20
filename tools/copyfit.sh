#!/bin/bash

# --- Automation Settings ---
TARGET_LABEL="opirv2"
# This is the standard path for Linux to identify devices by label
DEV_PATH="/dev/disk/by-label/$TARGET_LABEL"
# Recommended to mount at /mnt to avoid /media permissions being locked by desktop system
MOUNT_PATH="/mnt/opirv2"

# 1. Check if there are input parameters
if [ -z "$1" ]; then
    echo "Error: Please provide file path."
    echo "Usage: $0 <kernel.fit_path>"
    exit 1
fi

FILE_SOURCE=$1

# 2. Check if source file exists
if [ ! -f "$FILE_SOURCE" ]; then
    echo "Error: File '$FILE_SOURCE' not found"
    exit 1
fi

# 3. Check if SD card exists (via Label)
if [ ! -e "$DEV_PATH" ]; then
    echo "Error: Device with label '$TARGET_LABEL' not found, please confirm SD card is connected to the virtual machine."
    exit 1
fi

# 4. Prepare mount point
if [ ! -d "$MOUNT_PATH" ]; then
    echo "Creating mount point: $MOUNT_PATH"
    sudo mkdir -p "$MOUNT_PATH"
fi

# 5. Execute mount (if not already mounted)
if ! mountpoint -q "$MOUNT_PATH"; then
    echo "Mounting $DEV_PATH..."
    sudo mount "$DEV_PATH" "$MOUNT_PATH"
fi

# 6. Copy file
echo "Copying $FILE_SOURCE to $MOUNT_PATH..."
sudo cp "$FILE_SOURCE" "$MOUNT_PATH/kernel.fit"

# 7. Force write and unmount
echo "Syncing data..."
sync
echo "Unmounting device..."
sudo umount "$MOUNT_PATH"

if [ $? -eq 0 ]; then
    echo "Completed! File has been successfully written, you can safely remove the SD card."
else
    echo "Warning during unmount, please confirm no other windows have the directory open."
fi