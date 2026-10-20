# Pull-based deploy to a Debian DerbyNet server

The server pulls this fork from GitHub and copies `website/` into
`/var/www/html/derbynet`. Nothing is pushed to the server.

## One-time setup (on the server)

```bash
sudo apt-get install -y git rsync
sudo git clone https://github.com/zachwatts94/derbynet.git /opt/derbynet-fork
sudo install -m 755 /opt/derbynet-fork/extras/deploy/pull-deploy.sh /usr/local/bin/derbynet-deploy
```

## Each update

```bash
sudo derbynet-deploy              # deploys branch event-setup
sudo derbynet-deploy some-branch  # or another branch
```

The server needs internet access while deploying, so do it before the event.

## What it does and doesn't touch

- Copies everything under `website/` except `local/`.
- Never deletes files from the live site.
- Leaves the database and photos (`/var/lib/derbynet`) alone.
- A later `apt upgrade` of `derbynet-server` can overwrite deployed files;
  re-run `derbynet-deploy` afterwards.
