#!/bin/sh
# Fetches open-source ActionScript 3 projects used as an extra test corpus.
# They are not part of this repository: each lands in compiler/tests/external/<name>, which git ignores.
# Each is pinned to one commit so that results are repeatable.
#
#   name          licence      commit
#   as3-signals   MIT          975eefc416bdc36513a39cae0339dc205d937922
#   as3corelib    BSD 3-clause 24c6c16aecbf0d8fcc043ae671e689b0d4b4c559
#   starling      BSD 2-clause ffde39a2d459997fe5b7e4bf23b6a57928995b3a
#   flixel        MIT          8989e5044be072c4abbbaa1317c9854786f6447f
#   flashpunk     MIT          46faa60129749725054226a30acd11d97afece6c
#   away3d        Apache 2.0   26739318fe555a115bf47bd7b865bace09743682
set -eu
root="$(cd "$(dirname "$0")" && pwd)/external"
mkdir -p "$root"

fetch() {
	name="$1"
	url="$2"
	commit="$3"
	if [ -d "$root/$name/.git" ]; then
		echo "$name: already fetched"
		return
	fi
	mkdir -p "$root/$name"
	git -C "$root/$name" init -q
	git -C "$root/$name" fetch -q --depth 1 "$url" "$commit"
	git -C "$root/$name" -c advice.detachedHead=false checkout -q FETCH_HEAD
	echo "$name: fetched $commit"
}

fetch as3-signals https://github.com/robertpenner/as3-signals 975eefc416bdc36513a39cae0339dc205d937922
fetch as3corelib https://github.com/mikechambers/as3corelib 24c6c16aecbf0d8fcc043ae671e689b0d4b4c559
fetch starling https://github.com/Gamua/Starling-Framework ffde39a2d459997fe5b7e4bf23b6a57928995b3a
fetch flixel https://github.com/AdamAtomic/flixel 8989e5044be072c4abbbaa1317c9854786f6447f
fetch flashpunk https://github.com/useflashpunk/FlashPunk 46faa60129749725054226a30acd11d97afece6c
fetch away3d https://github.com/away3d/away3d-core-fp11 26739318fe555a115bf47bd7b865bace09743682
