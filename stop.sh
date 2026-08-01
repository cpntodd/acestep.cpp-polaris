#!/bin/bash
# stop.sh — Stop the ACE-Step supervisor started by start.sh.
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIDFILE="${DIR}/server.pid"

# Ask the supervisor to stop its child first, so model memory and the worker
# thread are released cleanly. The supervisor remains available for the WebUI
# switch until the user exits the app or this script kills it below.
curl --silent --fail --max-time 5 -X POST http://127.0.0.1:8081/stop >/dev/null 2>&1 || true

if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
	kill "$(cat "$PIDFILE")"
	for _ in $(seq 1 50); do
		if ! kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
			break
		fi
		sleep 0.1
	done
	find "$DIR" -maxdepth 1 -name 'server.pid' -type f -delete
	echo "[ACE-Step] Server supervisor stopped."
else
	find "$DIR" -maxdepth 1 -name 'server.pid' -type f -delete
	echo "[ACE-Step] No tracked server supervisor is running."
fi
