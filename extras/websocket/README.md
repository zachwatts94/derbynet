# Websocket relay (needed for two replay screens)

Without it, each replay command goes into one shared queue and only whichever
replay machine polls first plays it.  With it, one "trigger replay" reaches every
replay machine at once.

Server setup (Debian 13), as done for Pack 525:

1. `sudo apt-get install nodejs node-ws`
2. Copy `ws-server.js` to `/opt/derbynet-ws/`.  It listens on 127.0.0.1 only
   (8010 websocket, 8020 trigger from PHP), so nothing on the network can talk to it.
3. systemd unit `derbynet-ws.service`: `Environment=NODE_PATH=/usr/share/nodejs`,
   `ExecStart=/usr/bin/node /opt/derbynet-ws/ws-server.js`, `User=www-data`, `Restart=always`.
4. In `/etc/nginx/derbynet/location.snippet` add:
   `location = /ws { proxy_pass http://127.0.0.1:8010; proxy_http_version 1.1;
   proxy_set_header Upgrade $http_upgrade; proxy_set_header Connection "upgrade";
   proxy_read_timeout 3600s; }`
5. RaceInfo (stored in the database, so a template database carries it):
   `_websocket_url` = `wss://derbynet.local/ws`, `_ws_trigger_port` = `8020:127.0.0.1`.
6. Restart the replay pages (close chromium; the kiosk script reopens it).
