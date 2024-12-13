#!/bin/bash
EXTENSIONS="*.cc *.h"
for ext in $EXTENSIONS; do
    find . -type f -name "$ext" -exec astyle --style=google --suffix=none {} \;
done
find . -type f -name "*.orig" -delete
echo "Formatting completed!"
