#!/bin/sh

root=${FAKE_RELEASES:-/srv/hikarisub-releases}
prefix=https://github.com/altqx/hikari/releases/download/
output=
url=
https_only=0
previous=
for argument in "$@"; do
	case $previous in
		--output|-o) output=$argument ;;
		--proto) [ "$argument" = =https ] && https_only=1 ;;
	esac
	case $argument in
		https://*|http://*) url=$argument ;;
	esac
	previous=$argument
done
printf '%s\n' "$url" >> "$root/requests.log"
if [ -n "${FAKE_CURL_OFFLINE:-}" ]; then
	echo "fake curl: (6) Could not resolve host: github.com" >&2
	exit 6
fi
[ "$https_only" -eq 1 ] || { echo "fake curl: not https only" >&2; exit 1; }
case $url in
	"$prefix"*) ;;
	*) echo "fake curl: not a release URL: $url" >&2; exit 6 ;;
esac
path=${url#"$prefix"}
case $path in
	*..*) echo "fake curl: bad path" >&2; exit 3 ;;
esac
[ -f "$root/$path" ] || { echo "fake curl: (22) The requested URL returned error: 404" >&2; exit 22; }
if [ -n "$output" ]; then
	cp "$root/$path" "$output"
else
	cat "$root/$path"
fi
