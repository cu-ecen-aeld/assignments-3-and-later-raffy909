#!/bin/sh

if [ $# -lt 2 ]
then
    echo "usage: $0 <path> <search-string>"
    exit 1
fi

if [ ! -d $1 ]
then
    echo "first parameter has to be a valid directory"
    exit 1
fi

total_items=$(find $1 -type f | wc -l)
matched_items=$(grep -r $2 $1 | wc -l)

echo "The number of files are $total_items and the number of matching lines are $matched_items"