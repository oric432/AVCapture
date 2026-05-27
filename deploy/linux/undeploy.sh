#!/usr/bin/env bash
set -euo pipefail

APP_NAME="VSCapture"
SERVICE_FILE="${HOME}/.config/systemd/user/${APP_NAME}.service"

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
    echo "systemctl not found. Cannot remove systemd user service."
    exit 1
fi

systemctl --user stop "${APP_NAME}.service" >/dev/null 2>&1 || true
systemctl --user disable "${APP_NAME}.service" >/dev/null 2>&1 || true
pkill -TERM -x "${APP_NAME}" >/dev/null 2>&1 || true
rm -f "${SERVICE_FILE}"
systemctl --user daemon-reload

echo "Removed \"${APP_NAME}\"."
