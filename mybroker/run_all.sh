#!/bin/bash

cd /service
./fileup  &
./fileman &
./kvstore &
./inventory &
./integrity.dist/integrity.bin &
. .venv/bin/activate
gunicorn --bind unix:/service/run.sock web.wsgi:app --daemon
/usr/sbin/nginx -g "daemon off;" &
socat - tcp6-connect:[::1]:11342,retry=10,interval=1 2>/dev/null

