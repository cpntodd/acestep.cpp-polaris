#!/bin/bash
# start.sh — Start the ACE-Step supervisor (background) and open the WebUI.
# The supervisor keeps the WebUI control switch usable after a server crash.
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT="8080"
CONTROL_PORT="8081"
URL="http://localhost:${PORT}"
LOG="${DIR}/server.log"
PIDFILE="${DIR}/server.pid"

SUPERVISOR="${DIR}/build/acestep-supervisor"
SERVER="${DIR}/build/ace-server"

open_ui() {
    xdg-open "$URL" >/dev/null 2>&1 || true
}

supervisor_pid_is_ours() {
	[ -f "$PIDFILE" ] || return 1
	local pid command
	pid="$(cat "$PIDFILE" 2>/dev/null || true)"
	[[ "$pid" =~ ^[0-9]+$ ]] || return 1
	kill -0 "$pid" 2>/dev/null || return 1
	command="$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null || true)"
	[[ "$command" == *"acestep-supervisor"* ]]
}

# If our tracked supervisor is still alive, start its child if needed and open
# the browser. This also repairs a server that was stopped from the WebUI.
if supervisor_pid_is_ours; then
	if ! curl --silent --fail --max-time 2 "$URL/health" >/dev/null 2>&1; then
		curl --silent --fail --max-time 2 -X POST "http://127.0.0.1:${CONTROL_PORT}/start" >/dev/null 2>&1 || true
	fi
	for _ in $(seq 1 30); do
		if curl --silent --fail --max-time 2 "$URL/health" >/dev/null 2>&1; then
			echo "[ACE-Step] Server already supervised. Opening browser..."
			open_ui
			exit 0
		fi
		sleep 1
	done
	echo "[ACE-Step] Existing supervisor is not responding; stopping the stale local process."
	kill "$(cat "$PIDFILE")" 2>/dev/null || true
	find "$PIDFILE" -maxdepth 0 -type f -delete
fi

# A previous launcher may have left a PID file after its process was reaped.
find "$PIDFILE" -maxdepth 0 -type f -delete 2>/dev/null || true

# Port already answered by something (for example, a manually started server).
if curl -s -o /dev/null --max-time 2 "$URL" 2>/dev/null; then
	echo "[ACE-Step] Something already responds on ${URL}. Opening browser..."
	open_ui
	exit 0
fi

# Check the binaries exist.
if [ ! -x "$SERVER" ] || [ ! -x "$SUPERVISOR" ]; then
	echo "[ACE-Step] ERROR: build/ace-server or build/acestep-supervisor not found. Build the project first." >&2
	exit 1
fi

echo "[ACE-Step] Starting supervisor for ${URL} (log: ${LOG}) ..."
nohup "$SUPERVISOR" \
	--server "$SERVER" \
	--server-port "$PORT" \
	--control-port "$CONTROL_PORT" \
	--models "${DIR}/models" \
	--adapters "${DIR}/adapters" \
	>>"${LOG}" 2>&1 &
echo $! > "${PIDFILE}"

# Wait up to 60 s for the server to become ready.
for i in $(seq 1 60); do
	if curl --silent --fail --max-time 2 "$URL/health" >/dev/null 2>&1; then
		echo "[ACE-Step] Server ready after ${i}s (supervisor PID $(cat "$PIDFILE")). Opening browser..."
		open_ui
		exit 0
    fi
    sleep 1
done

echo "[ACE-Step] Server did not become ready in time. Check ${LOG}" >&2
exit 1
