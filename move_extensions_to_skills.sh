#!/bin/bash

# Script to move extensions to skills directory
# This script should be run manually

echo "Moving extensions to skills directory..."

# Create skills directory if it doesn't exist
mkdir -p ~/.cclaw/skills

# Check if extensions directory exists
if [ -d "~/.cclaw/extentions" ]; then
    echo "Found extensions directory at ~/.cclaw/extentions"
    
    # Copy all extensions to skills directory
    for ext in ~/.cclaw/extentions/*; do
        if [ -d "$ext" ]; then
            ext_name=$(basename "$ext")
            echo "Copying $ext_name..."
            cp -r "$ext" ~/.cclaw/skills/
        fi
    done
    
    echo "Done! Copied all extensions to ~/.cclaw/skills/"
    echo "Total items copied: $(ls ~/.cclaw/skills/ | wc -l)"
else
    echo "Extensions directory not found at ~/.cclaw/extentions"
    echo "Checking for ~/.cclaw/extensions..."
    
    if [ -d "~/.cclaw/extensions" ]; then
        echo "Found extensions directory at ~/.cclaw/extensions"
        
        # Copy all extensions to skills directory
        for ext in ~/.cclaw/extensions/*; do
            if [ -d "$ext" ]; then
                ext_name=$(basename "$ext")
                echo "Copying $ext_name..."
                cp -r "$ext" ~/.cclaw/skills/
            fi
        done
        
        echo "Done! Copied all extensions to ~/.cclaw/skills/"
        echo "Total items copied: $(ls ~/.cclaw/skills/ | wc -l)"
    else
        echo "No extensions directory found"
    fi
fi

# List the skills directory
echo "\nContents of ~/.cclaw/skills/:"
ls -la ~/.cclaw/skills/