#!/bin/bash
# Gets Shattered Pixel Dungeon's source (GPLv3) at the version this fork is built on.
set -e
cd "$(dirname "$0")"
TAG=v4.0.2
[ -d spd ] || git -c advice.detachedHead=false clone -q --depth 1 --branch $TAG https://github.com/00-Evan/shattered-pixel-dungeon spd
mkdir -p lib && cd lib
M=https://repo1.maven.org/maven2/com/badlogicgames
for p in gdx/gdx/1.14.0/gdx-1.14.0.jar gdx/gdx-backend-headless/1.14.0/gdx-backend-headless-1.14.0.jar \
  gdx/gdx-platform/1.14.0/gdx-platform-1.14.0-natives-desktop.jar gdx/gdx-freetype/1.14.0/gdx-freetype-1.14.0.jar \
  gdx/gdx-freetype-platform/1.14.0/gdx-freetype-platform-1.14.0-natives-desktop.jar \
  gdx/gdx-jnigen-loader/2.5.2/gdx-jnigen-loader-2.5.2.jar gdx-controllers/gdx-controllers-core/2.2.4/gdx-controllers-core-2.2.4.jar; do
  [ -f "$(basename $p)" ] || curl -sSfLO $M/$p
done
[ -f json-20170516.jar ] || curl -sSfLO https://repo1.maven.org/maven2/org/json/json/20170516/json-20170516.jar
