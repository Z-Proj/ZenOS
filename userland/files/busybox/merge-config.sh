#!/bin/sh
set -eu

config_path=$1
fragment_path=$2
tmp_path=$(mktemp)
cp "$config_path" "$tmp_path"

while IFS= read -r line || [ -n "$line" ]; do
	case "$line" in
	'')
		continue
		;;
	'# CONFIG_'*' is not set')
		key=$(printf '%s\n' "$line" | sed -n 's/^# \(CONFIG_[^ ]*\) is not set$/\1/p')
		;;
	'# '*)
		continue
		;;
	CONFIG_*=*)
		key=${line%%=*}
		;;
	*)
		continue
		;;
	esac

	if grep -q "^$key=" "$tmp_path"; then
		sed -i "s|^$key=.*|$line|" "$tmp_path"
		continue
	fi

	if grep -q "^# $key is not set$" "$tmp_path"; then
		sed -i "s|^# $key is not set$|$line|" "$tmp_path"
		continue
	fi

	printf '%s\n' "$line" >> "$tmp_path"
done < "$fragment_path"

mv "$tmp_path" "$config_path"
