#!/bin/bash
set -e

./build.sh win
name="0.5players"
if [ -d "$name" ]
then
    rm -r "$name"
fi
if [ -f "${name}.zip" ]
then
    rm "${name}.zip"
fi
mkdir "$name"
mv a.exe "$name/0.5players.exe"
mkdir "$name/assets"
cp controls.txt assets/replays.bin "$name/"
cp assets/{tiles.glb,walls.png,wires.png,level.bin} "$name/assets/"
zip -r "${name}.zip" "$name"

echo "wrote ${name}.zip"
