#!/bin/sh

if [ $# -lt 2 ]
then
    echo "usage: $0 <path file to write> <string to write>"
    exit 1
fi

writefile=$1
writestr=$2

filedir=$(dirname $1)

if [ ! -d $filedir ]
then
    mkdir -p $filedir
fi

echo $writestr > $writefile

if [ $? -eq 1 ]
then
    echo "file write unsuccessful"
    exit 1
fi