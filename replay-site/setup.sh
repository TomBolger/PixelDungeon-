#!/bin/bash
# Pebxel replay site: one-time setup on a fresh Ubuntu droplet (also safe to run again to update).
#   bash setup.sh yourname.duckdns.org
set -e
DOMAIN="$1"
if [ -z "$DOMAIN" ]; then echo "Usage: bash setup.sh yourname.duckdns.org"; exit 1; fi
SRC="$(cd "$(dirname "$0")" && pwd)"
SITE=/srv/pebxel
echo "== Setting up $DOMAIN"

# 1. Caddy (the web server that also handles HTTPS)
if ! command -v caddy >/dev/null; then
  echo "== Installing Caddy"
  apt-get update -q
  apt-get install -y -q debian-keyring debian-archive-keyring apt-transport-https curl gnupg python3
  curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' | gpg --dearmor --yes -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
  curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt' > /etc/apt/sources.list.d/caddy-stable.list
  apt-get update -q
  apt-get install -y -q caddy
fi

# 2. The site files (the feedback notes in $SITE/data are never touched)
echo "== Copying the site"
mkdir -p $SITE/data /var/log/caddy
rm -rf $SITE/www.new && cp -r "$SRC/www" $SITE/www.new
gzip -kf9 $SITE/www.new/games/*.json $SITE/www.new/assets.json $SITE/www.new/index.html
rm -rf $SITE/www.old; [ -d $SITE/www ] && mv $SITE/www $SITE/www.old; mv $SITE/www.new $SITE/www; rm -rf $SITE/www.old
cp "$SRC/server.py" $SITE/server.py
id pebxel >/dev/null 2>&1 || useradd --system --home $SITE --shell /usr/sbin/nologin pebxel
chown -R pebxel:pebxel $SITE/data

# 3. The feedback service (starts on boot, restarts if it crashes)
cat > /etc/systemd/system/pebxel.service <<UNIT
[Unit]
Description=Pebxel replay feedback API
After=network.target
[Service]
User=pebxel
Environment=PEBXEL_DATA=$SITE/data
ExecStart=/usr/bin/python3 $SITE/server.py
Restart=always
RestartSec=3
NoNewPrivileges=true
ProtectSystem=strict
ReadWritePaths=$SITE/data
[Install]
WantedBy=multi-user.target
UNIT
systemctl daemon-reload
systemctl enable -q pebxel
systemctl restart pebxel

# 4. Web server config
sed -e "s#DOMAIN#$DOMAIN#" -e "s#SITEDIR#$SITE#" "$SRC/Caddyfile.template" > /etc/caddy/Caddyfile
chown -R caddy:caddy /var/log/caddy 2>/dev/null || true
caddy validate --config /etc/caddy/Caddyfile --adapter caddyfile >/dev/null
systemctl enable -q caddy
systemctl reload caddy 2>/dev/null || systemctl restart caddy

# 5. Firewall: only SSH and the web
if command -v ufw >/dev/null; then ufw allow OpenSSH >/dev/null; ufw allow 80/tcp >/dev/null; ufw allow 443/tcp >/dev/null; ufw --force enable >/dev/null; fi

sleep 2
KEY=$(cat $SITE/data/admin_key.txt)
echo
echo "================================================================"
echo " Done. Your site:      https://$DOMAIN"
echo " Read the feedback:    https://$DOMAIN/api/feedback?key=$KEY"
echo " (Keep that second link private. It's also saved in $SITE/data/admin_key.txt)"
echo " The first visit can take up to a minute while the HTTPS certificate is issued."
echo "================================================================"
