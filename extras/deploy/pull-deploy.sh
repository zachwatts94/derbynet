#!/bin/bash
# Pull this fork on the DerbyNet server and copy website/ into the live site.
#
# Run ON THE SERVER:   derbynet-deploy [branch]
# One-time setup:      see extras/deploy/README.md
#
# The site's local/ directory (config-roles.inc, config-database.inc) is never
# touched, and nothing is deleted from the live site.
set -eu

REPO="${DERBYNET_FORK_DIR:-/opt/derbynet-fork}"
SITE="${DERBYNET_SITE_DIR:-/var/www/html/derbynet}"
BRANCH="${1:-event-setup}"

[ "$(id -u)" -eq 0 ] || { echo "Run with sudo"; exit 1; }
[ -d "$REPO/.git" ] || { echo "No clone at $REPO (see extras/deploy/README.md)"; exit 1; }
[ -d "$SITE/local" ] || { echo "$SITE doesn't look like a DerbyNet site (no local/ dir)"; exit 1; }

cd "$REPO"
git fetch --quiet origin
git checkout --quiet "$BRANCH"
git reset --quiet --hard "origin/$BRANCH"

rsync -rlt --no-owner --no-group --chmod=D755,F644 \
      --exclude 'local/' \
      website/ "$SITE/"

echo "Deployed $(git rev-parse --short HEAD) from branch $BRANCH:"
git log -1 --format='  %s'
