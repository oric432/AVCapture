#!/usr/bin/env bash
set -euo pipefail

APP_NAME="AVCapture"
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXE="${SRC_DIR}/${APP_NAME}"
SERVICE_DIR="${HOME}/.config/systemd/user"
SERVICE_FILE="${SERVICE_DIR}/${APP_NAME}.service"

rerun_as_login_user() {
    if [ "${EUID}" -ne 0 ]; then
        return
    fi

    if [ -z "${SUDO_USER:-}" ] || [ "${SUDO_USER}" = "root" ]; then
        echo "Do not run this script as root. Run it as the desktop user."
        exit 1
    fi

    local user_uid
    local user_home
    user_uid="$(id -u "${SUDO_USER}")"
    user_home="$(getent passwd "${SUDO_USER}" | cut -d: -f6)"

    if [ ! -d "/run/user/${user_uid}" ]; then
        echo "Cannot find the user systemd runtime directory for ${SUDO_USER}."
        echo "Run this script from an active desktop login session."
        exit 1
    fi

    exec sudo -u "${SUDO_USER}" \
        HOME="${user_home}" \
        XDG_RUNTIME_DIR="/run/user/${user_uid}" \
        "$0" "$@"
}

rerun_as_login_user "$@"

if ! command -v systemctl >/dev/null 2>&1; then
    echo "systemctl not found. This deploy script requires systemd user services."
    exit 1
fi

if [ ! -f "${EXE}" ]; then
    echo "${APP_NAME} executable not found in the current folder. Please include ${APP_NAME}."
    exit 1
fi

chmod +x "${EXE}"
mkdir -p "${SERVICE_DIR}"

cat > "${SERVICE_FILE}" <<EOF
[Unit]
Description=${APP_NAME}
After=graphical-session.target

[Service]
Type=simple
WorkingDirectory=${SRC_DIR}
ExecStart=${EXE} --background
Restart=on-failure
RestartSec=60
KillSignal=SIGTERM
TimeoutStopSec=30

[Install]
WantedBy=default.target
EOF

systemctl --user import-environment DISPLAY XAUTHORITY WAYLAND_DISPLAY XDG_CURRENT_DESKTOP DBUS_SESSION_BUS_ADDRESS || true
systemctl --user daemon-reload
systemctl --user enable --now "${APP_NAME}.service"

echo "Installed \"${APP_NAME}\" as a user systemd service."
echo "Status: systemctl --user status ${APP_NAME}.service"

# --------------- Tray app ----------------
# The tray is a normal desktop GUI app, not a background service, so it
# autostarts via the standard XDG mechanism rather than a systemd unit.
TRAY_EXE="${SRC_DIR}/${APP_NAME}Tray"
AUTOSTART_DIR="${HOME}/.config/autostart"
AUTOSTART_FILE="${AUTOSTART_DIR}/${APP_NAME}Tray.desktop"

if [ -f "${TRAY_EXE}" ]; then
    chmod +x "${TRAY_EXE}"
    mkdir -p "${AUTOSTART_DIR}"

    cat > "${AUTOSTART_FILE}" <<EOF
[Desktop Entry]
Type=Application
Name=${APP_NAME} Tray
Exec=${TRAY_EXE}
X-GNOME-Autostart-enabled=true
EOF

    pkill -TERM -x "${APP_NAME}Tray" >/dev/null 2>&1 || true
    nohup "${TRAY_EXE}" >/dev/null 2>&1 &
    disown

    echo "Installed \"${APP_NAME} Tray\" autostart entry and launched it."
else
    echo "${APP_NAME}Tray executable not found in the current folder. Skipping tray autostart."
fi
