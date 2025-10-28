#!/bin/bash

# maven repository URL
repo_url="https://repo1.maven.org/maven2/com/google/javascript/closure-compiler"

# find the latest version
latest_version=$(curl -s "$repo_url/" | grep -oE 'v[0-9]{8}' | sort -nr | head -n 1)

if [ -z "$latest_version" ]; then
  echo "error: unable to determine the latest version of closure compiler."
  exit 1
fi

echo "Latest compiler version is: $latest_version"

latest_compiler="closure-compiler-$latest_version.jar"
download_url="$repo_url/$latest_version/$latest_compiler"

if [ -e "$latest_compiler" ]; then
  echo "Already have the latest compiler"
else
  curl --silent "$download_url" > "$latest_compiler"
fi

echo Compiling

java -jar "$latest_compiler" \
  --compilation_level ADVANCED_OPTIMIZATIONS \
  --strict_mode_input \
  --js main.js \
  --js_output_file j.js

sed -e 's/^[ \t]\+//' style.css | tr -d '\n\r\t' > s.css
