#!/usr/bin/env bash
#
# 拉取当前 PHP 版本对应的 fufake.so
#
#   bash get-so.sh                 # 自动匹配本机 php
#   PHPBIN=/www/server/php/74/bin/php bash get-so.sh
#   bash get-so.sh v1.0.0          # 指定版本
#
set -euo pipefail

REPO="YeXuanHs/fufake"
TAG="${1:-latest}"
PHPBIN="${PHPBIN:-$(command -v php || true)}"

if [ -z "$PHPBIN" ] || [ ! -x "$PHPBIN" ]; then
    echo "找不到 php，可用 PHPBIN=/path/to/php 指定" >&2
    exit 1
fi

VER="$("$PHPBIN" -r 'echo PHP_MAJOR_VERSION . "." . PHP_MINOR_VERSION;')"
case "$VER" in
    7.2|7.3|7.4) ;;
    *) echo "不支持的 PHP 版本：$VER（仅 7.2 / 7.3 / 7.4）" >&2; exit 1 ;;
esac

case "$(uname -m)" in
    x86_64|amd64) ARCH=x86_64 ;;
    *) echo "不支持的架构：$(uname -m)" >&2; exit 1 ;;
esac

SO="fufake-php${VER}-linux-${ARCH}.so"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

fetch() {
    if command -v gh >/dev/null 2>&1 && gh auth status >/dev/null 2>&1; then
        # gh 不认 "latest" 这个 tag，最新版要省略该参数
        if [ "$TAG" = "latest" ]; then
            gh release download -R "$REPO" -D "$WORK" -p "$1" 2>/dev/null && return 0
        else
            gh release download "$TAG" -R "$REPO" -D "$WORK" -p "$1" 2>/dev/null && return 0
        fi
    fi
    local url
    if [ "$TAG" = "latest" ]; then
        url="https://github.com/${REPO}/releases/latest/download/$1"
    else
        url="https://github.com/${REPO}/releases/download/${TAG}/$1"
    fi
    if [ -n "${GITHUB_TOKEN:-}" ]; then
        curl -fsSL -H "Authorization: token ${GITHUB_TOKEN}" -o "$WORK/$1" "$url"
    else
        curl -fsSL -o "$WORK/$1" "$url"
    fi
}

echo "==> PHP ${VER} / ${ARCH}，拉取 ${SO}（${TAG}）"
fetch "$SO"
fetch SHA256SUMS || true

cd "$WORK"
if [ -f SHA256SUMS ]; then
    grep " ${SO}\$" SHA256SUMS > sums.txt 2>/dev/null || cp SHA256SUMS sums.txt
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum -c sums.txt
    fi
fi

EXTDIR="$("$PHPBIN" -r 'echo ini_get("extension_dir");')"
DEST="${1:-}"
echo
echo "已下载：$WORK/$SO"
[ -n "$EXTDIR" ] && echo "extension_dir：$EXTDIR"
echo
echo "安装："
echo "  cp $WORK/$SO \"${EXTDIR:-<extension_dir>}/fufake.so\""
echo "  echo 'extension=fufake.so' >> \$(php --ini | awk -F': ' '/Loaded Configuration/{print \$2}')"
echo "  重启 php-fpm 后执行：php --ri fufake"
