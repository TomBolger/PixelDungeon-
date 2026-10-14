#!/bin/sh
# package_site.sh: bundle the site (viewer, recorded games, feedback server, setup script) into
# replay-site/dist/pebxel-site.tgz, the one file DEPLOY.txt tells you to upload to the server.
set -e
cd "$(dirname "$0")/.."
[ -f www/games/index.json ] || { echo "No games yet: run tools/record_games.sh first"; exit 1; }
T=$(mktemp -d); mkdir -p "$T/pebxel-site" dist
cp -r www server.py setup.sh Caddyfile.template "$T/pebxel-site/"; cp DEPLOY.txt "$T/pebxel-site/README.txt"
tar czf dist/pebxel-site.tgz -C "$T" pebxel-site; rm -rf "$T"
ls -la dist/pebxel-site.tgz
