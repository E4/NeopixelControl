#!/bin/bash

# Create a style map
grep '{' "style.css" \
| sed 's/{.*//' \
| grep -o '\.[A-Za-z0-9_-]\+' \
| sed 's/^\.//' \
| sort -u \
| awk '!seen[$0]++' | awk '
function idx_to_letters(n,   s, c) {
  while (n > 0) {
    n--; c = sprintf("%c", 97 + (n % 26))
    s = c s
    n = int(n / 26)
  }
  return s
}

{ print $0, idx_to_letters(NR) }
' > style.map

# Build a sed script from the map
awk '{
  printf "s/\\.%s([^A-Za-z0-9_-])/\\.%s\\1/g\n", $1, $2;  # .foo, followed by non ident
  printf "s/\\.%s$/\\.%s/g\n",           $1, $2;         # .foo at end of line
}' style.map > css.sed

# Apply to your CSS
sed -E -f css.sed style.css > style.min.css

awk '{
  printf "s/(^|[^A-Za-z0-9_-])\"%s\"([^A-Za-z0-9_-]|$)/\\1\"%s\"\\2/g\n", $1,$2
  printf "s/(^|[^A-Za-z0-9_-])'\''%s'\''([^A-Za-z0-9_-]|$)/\\1'\''%s'\''\\2/g\n", $1,$2
}' style.map > js.sed

# Review before applying; string handling in JS is messy by nature.
sed -E -f js.sed main.js > main.min.js



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
  --js main.min.js \
  --js_output_file j.js

sed -e 's/^[ \t]\+//' style.min.css | tr -d '\n\r\t' > s.css
