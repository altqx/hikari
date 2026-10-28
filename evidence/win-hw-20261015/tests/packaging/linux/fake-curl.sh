#!/bin/sh
# A stand-in for curl in the update helper's tests, installed as
# /usr/local/bin/curl in a test container, ahead of the real one in the
# helper's PATH. It serves the GitHub release URLs the helper asks for from
# a folder, so no test reaches the network:
#
#   https://github.com/altqx/hikari/releases/download/<tag>/<name>
#     -> $FAKE_RELEASES/<tag>/<name>  (default /srv/hikarisub-releases)
#
# It logs each URL to $FAKE_RELEASES/requests.log, fails as curl --fail
# does (22) for a file that is not there, and refuses to serve a request
# that does not insist on https, as the helper's must. FAKE_CURL_OFFLINE=1
# fails every request as an unreachable host does (6).

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
