#!/usr/bin/env bash
# Prepares the Ubuntu environment of the GitHub Copilot cloud agent for the Inno Setup plugin work.
# Called step by step from .github/workflows/copilot-setup-steps.yml:
#   setup-inno-tools.sh packages | sevenzip | iscc | old | sources | innoextract | smoke
#
# Installed tools (documented in .github/prompts/inno-plugin-refactor.prompt.md):
#   iscc-<version>        Inno Setup command-line compilers running under Wine
#   iscc-<old version>    old compilers (1.3.26 to 5.5.9) unpacked from the installers below
#   /opt/inno-tools/old   installers of the old Inno Setup versions (InnoPlugin/tests/old-versions.txt)
#   innoextract           innoextract 1.9 from Ubuntu
#   innoextract-master    innoextract built from the pinned master commit below
#   7zz                   7-Zip for Linux
#   7z-win                Windows 7-Zip x64 under Wine; plugins go to $TOOLS/7zip-win64/Formats
#   ISCrypt.dll           official ARCFOUR DLL in the folders of the compilers before 6.4 (Encryption=yes)
#   /opt/src/{7zip,innoextract,issrc}   reference source checkouts with full history and tags
set -euo pipefail

TOOLS=/opt/inno-tools
SRC=/opt/src
SEVENZIP_TAG=26.05
INNOEXTRACT_COMMIT=6e9e34ed0876014fdb46e684103ef8c3605e382e
ISCRYPT_URLS=(https://jrsoftware.org/download.php/iscrypt.dll https://files.innosetup.nl/ISCrypt.dll)
ISCRYPT_SHA256=2f6294f9aa09f59a574b5dcd33be54e16b39377984f3d5658cda44950fa0f8fc
# Old Inno Setup versions: folder, file name and SHA-256 of each installer.
OLD_LIST="$(dirname "$0")/../../InnoPlugin/tests/old-versions.txt"
OLD_URL=https://files.jrsoftware.org/is
# The old compilers of run_tests.py (OLD_COMPILERS), unpacked from the installers.
OLD_ISCC=(1.3.26 2.0.19 3.0.7 4.0.8 4.0.11 4.1.5 4.1.8 4.2.7 5.1.14 5.2.3 5.3.8 5.3.8-unicode 5.5.9 5.5.9-unicode)

export WINEPREFIX="${WINEPREFIX:-$HOME/.wine}"
export WINEDEBUG=-all
export WINEDLLOVERRIDES="mscoree,mshtml="

# label|url|extra installer arguments
ISCC_VERSIONS=(
  "5.6.1|https://files.jrsoftware.org/is/5/innosetup-5.6.1.exe|"
  "5.6.1-unicode|https://files.jrsoftware.org/is/5/innosetup-5.6.1-unicode.exe|"
  "6.0.5|https://github.com/jrsoftware/issrc/releases/download/is-6_0_5/innosetup-6.0.5.exe|"
  "6.1.2|https://github.com/jrsoftware/issrc/releases/download/is-6_1_2/innosetup-6.1.2.exe|"
  "6.2.2|https://github.com/jrsoftware/issrc/releases/download/is-6_2_2/innosetup-6.2.2.exe|"
  "6.3.3|https://github.com/jrsoftware/issrc/releases/download/is-6_3_3/innosetup-6.3.3.exe|"
  "6.4.3|https://github.com/jrsoftware/issrc/releases/download/is-6_4_3/innosetup-6.4.3.exe|"
  "6.5.4|https://github.com/jrsoftware/issrc/releases/download/is-6_5_4/innosetup-6.5.4.exe|"
  "6.6.1|https://github.com/jrsoftware/issrc/releases/download/is-6_6_1/innosetup-6.6.1.exe|"
  "6.7.3|https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe|"
  "7.0.2-x64|https://github.com/jrsoftware/issrc/releases/download/is-7_0_2/innosetup-7.0.2-x64.exe|"
  "7.1.0-x64|https://github.com/jrsoftware/issrc/releases/download/is-7_1_0/innosetup-7.1.0-x64.exe|"
  "7.1.0-x86|https://github.com/jrsoftware/issrc/releases/download/is-7_1_0/innosetup-7.1.0-x86.exe|"
)

prepare_dirs() {
  sudo mkdir -p "$TOOLS" "$SRC"
  sudo chown "$(id -u):$(id -g)" "$TOOLS" "$SRC"
}

step_packages() {
  sudo dpkg --add-architecture i386
  sudo apt-get update -q
  sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends \
    wine wine64 wine32:i386 xvfb xauth \
    innoextract \
    cmake ninja-build g++ g++-mingw-w64 \
    libboost-iostreams-dev libboost-filesystem-dev libboost-date-time-dev \
    libboost-system-dev libboost-program-options-dev liblzma-dev zlib1g-dev libbz2-dev \
    xz-utils
  xvfb-run -a wineboot --init
  wineserver -w
}

# Wrapper for programs running under Wine: arguments naming existing files or
# directories (also ISCC /O<dir>) are converted to Windows paths.
write_wine_wrapper() {
  local name=$1 exe=$2
  cat > "/tmp/$name" <<EOF
#!/usr/bin/env bash
export WINEPREFIX="\${WINEPREFIX:-\$HOME/.wine}" WINEDEBUG="\${WINEDEBUG:--all}"
args=()
for a in "\$@"; do
  case "\$a" in
    /O?*) d="\${a:2}"; if [ -d "\$d" ]; then a="/O\$(winepath -w "\$d")"; fi ;;
    *) if [ -e "\$a" ]; then a="\$(winepath -w "\$a")"; fi ;;
  esac
  args+=("\$a")
done
exec wine '$exe' "\${args[@]}"
EOF
  sudo install -m 755 "/tmp/$name" "/usr/local/bin/$name"
}

# Wrapper for Compil32.exe /cc (Inno Setup 1.3 has no ISCC.exe) that compiles
# the script given as the only argument. Compil32 shows errors in a dialog and
# still exits with 0, so it runs with a time limit; without an X display
# (DISPLAY) it gets a virtual one.
write_compil32_wrapper() {
  local name=$1 exe=$2
  cat > "/tmp/$name" <<EOF
#!/usr/bin/env bash
export WINEPREFIX="\${WINEPREFIX:-\$HOME/.wine}" WINEDEBUG="\${WINEDEBUG:--all}"
script="\$(winepath -w "\$1")"
if [ -n "\${DISPLAY:-}" ]; then
  exec timeout -k 10 120 wine '$exe' /cc "\$script"
fi
exec xvfb-run -a timeout -k 10 120 wine '$exe' /cc "\$script"
EOF
  sudo install -m 755 "/tmp/$name" "/usr/local/bin/$name"
}

# Before 6.4, Encryption=yes needs ISCrypt.dll in the compiler folder. Installs
# the official DLL, checked by its SHA-256, into the folders of the compilers
# given as arguments; only if it cannot be downloaded, builds the ARCFOUR
# stand-in InnoPlugin/tests/iscrypt/iscrypt.c instead.
install_iscrypt() {
  local iscrypt=/tmp/ISCrypt.dll kind="" url label
  if [ -f "$iscrypt" ] && [ -f "$iscrypt.kind" ]; then
    kind=$(cat "$iscrypt.kind")
  fi
  if [ -z "$kind" ]; then
    for url in "${ISCRYPT_URLS[@]}"; do
      if ! curl -fsSL --retry 3 --retry-delay 5 --retry-all-errors -o "$iscrypt" "$url"; then
        echo "::warning::Cannot download ISCrypt.dll from $url"
      elif [ "$(sha256sum "$iscrypt" | cut -d' ' -f1)" != "$ISCRYPT_SHA256" ]; then
        echo "::warning::ISCrypt.dll from $url has an unexpected SHA-256"
      else
        kind=official
        break
      fi
    done
  fi
  if [ -z "$kind" ]; then
    echo "::warning::The official ISCrypt.dll is not available, building the stand-in"
    kind=stand-in
    if ! i686-w64-mingw32-gcc -O2 -shared -s -Wl,--kill-at -o "$iscrypt" \
         "$(dirname "$0")/../../InnoPlugin/tests/iscrypt/iscrypt.c"; then
      echo "::warning::Cannot build the ISCrypt.dll stand-in"
      rm -f "$iscrypt"
      return 0
    fi
  fi
  echo "$kind" > "$iscrypt.kind"
  for label in "$@"; do
    local dir="$WINEPREFIX/drive_c/iscc/$label"
    if [ -d "$dir" ]; then
      cp "$iscrypt" "$dir/ISCrypt.dll"
      echo "ISCrypt.dll ($kind) for iscc-$label"
    fi
  done
}

step_sevenzip() {
  prepare_dirs
  local base="https://github.com/ip7z/7zip/releases/download/$SEVENZIP_TAG"
  local ver="${SEVENZIP_TAG//./}"
  curl -fsSL -o /tmp/7z-linux.tar.xz "$base/7z$ver-linux-x64.tar.xz"
  mkdir -p "$TOOLS/7zip-linux"
  tar -xJf /tmp/7z-linux.tar.xz -C "$TOOLS/7zip-linux"
  sudo ln -sf "$TOOLS/7zip-linux/7zz" /usr/local/bin/7zz
  curl -fsSL -o /tmp/7z-win64.exe "$base/7z$ver-x64.exe"
  7zz x -y -o"$TOOLS/7zip-win64" /tmp/7z-win64.exe > /dev/null
  mkdir -p "$TOOLS/7zip-win64/Formats"
  write_wine_wrapper 7z-win "$(winepath -w "$TOOLS/7zip-win64/7z.exe")"
  7zz i > /tmp/7zz-info.txt && sed -n '1,3p' /tmp/7zz-info.txt
  7z-win i > /tmp/7zwin-info.txt && sed -n '1,6p' /tmp/7zwin-info.txt
}

step_iscc() {
  prepare_dirs
  local failed=()
  for entry in "${ISCC_VERSIONS[@]}"; do
    IFS='|' read -r label url extra <<< "$entry"
    echo "::group::Inno Setup $label"
    if curl -fsSL -o "/tmp/is-$label.exe" "$url" \
      && xvfb-run -a wine "/tmp/is-$label.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /NOICONS \
           "/DIR=C:\\iscc\\$label" $extra \
      && wineserver -w \
      && [ -f "$WINEPREFIX/drive_c/iscc/$label/ISCC.exe" ]; then
      write_wine_wrapper "iscc-$label" "C:\\iscc\\$label\\ISCC.exe"
      echo "installed iscc-$label"
    else
      failed+=("$label")
      echo "::warning::Inno Setup $label could not be installed"
    fi
    echo "::endgroup::"
  done
  if [ ${#failed[@]} -gt 0 ]; then
    echo "::warning::Missing Inno Setup compilers: ${failed[*]}"
  fi
  install_iscrypt 5.6.1 5.6.1-unicode 6.0.5 6.1.2 6.2.2 6.3.3
}

# Downloads the installers of the old Inno Setup versions (OLD_LIST) to
# $TOOLS/old, checked by their SHA-256, and unpacks the old compilers
# (OLD_ISCC) from them with innoextract instead of running their installers.
step_old() {
  prepare_dirs
  mkdir -p "$TOOLS/old"
  local failed=() folder name sha file label
  while read -r folder name sha; do
    case "$folder" in ''|\#*) continue ;; esac
    file="$TOOLS/old/$name"
    if [ -f "$file" ] && [ "$(sha256sum "$file" | cut -d' ' -f1)" = "$sha" ]; then
      continue
    fi
    if ! curl -fsSL --retry 3 --retry-delay 5 --retry-all-errors -o "$file" "$OLD_URL/$folder/$name" \
       || [ "$(sha256sum "$file" | cut -d' ' -f1)" != "$sha" ]; then
      rm -f "$file"
      failed+=("$name")
    fi
  done < "$OLD_LIST"
  echo "$(find "$TOOLS/old" -name '*.exe' | wc -l) old installers in $TOOLS/old"
  for label in "${OLD_ISCC[@]}"; do
    local src="" dir="$WINEPREFIX/drive_c/iscc/$label" tmp="/tmp/old-$label"
    for name in "isetup-$label.exe" "innosetup-$label.exe"; do
      if [ -f "$TOOLS/old/$name" ]; then src="$TOOLS/old/$name"; fi
    done
    rm -rf "$tmp" "$dir"
    mkdir -p "$tmp" "$(dirname "$dir")"
    if [ -n "$src" ] && innoextract -e -s -p0 -d "$tmp" "$src" && [ -d "$tmp/app" ]; then
      mv "$tmp/app" "$dir"
      if [ -f "$dir/ISCC.exe" ]; then
        write_wine_wrapper "iscc-$label" "C:\\iscc\\$label\\ISCC.exe"
      else
        write_compil32_wrapper "iscc-$label" "C:\\iscc\\$label\\compil32.exe"
      fi
      echo "unpacked iscc-$label"
    else
      failed+=("iscc-$label")
    fi
    rm -rf "$tmp"
  done
  if [ ${#failed[@]} -gt 0 ]; then
    echo "::warning::Old Inno Setup versions missing: ${failed[*]}"
  fi
  # Compilers without Encryption ignore the DLL.
  install_iscrypt "${OLD_ISCC[@]}"
}

step_sources() {
  prepare_dirs
  git clone -q https://github.com/ip7z/7zip "$SRC/7zip"
  git clone -q https://github.com/dscharrer/innoextract "$SRC/innoextract"
  git -C "$SRC/innoextract" checkout -q "$INNOEXTRACT_COMMIT"
  git clone -q https://github.com/jrsoftware/issrc "$SRC/issrc"
  git -C "$SRC/7zip" tag --list "$SEVENZIP_TAG"
  git -C "$SRC/issrc" tag --list 'is-7_*' | tail -n 3
}

step_innoextract() {
  cmake -S "$SRC/innoextract" -B /tmp/innoextract-build -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
  cmake --build /tmp/innoextract-build > /dev/null
  sudo install -m 755 /tmp/innoextract-build/innoextract /usr/local/bin/innoextract-master
  innoextract --version | sed -n '1p'
  innoextract-master --version | sed -n '1p'
}

# Compiles a one-file installer with every installed compiler and reports which reference
# extractor can list it. Fails only if a compiler is missing or broken.
step_smoke() {
  local dir=/tmp/inno-smoke status=0
  rm -rf "$dir" && mkdir -p "$dir"
  echo hello > "$dir/hello.txt"
  printf '%s\r\n' '[Setup]' 'AppName=Smoke' 'AppVersion=1.0' 'DefaultDirName={sd}\Smoke' \
    'OutputDir=.' 'Uninstallable=no' '[Files]' 'Source: "hello.txt"; DestDir: "{app}"' > "$dir/smoke.iss"
  for entry in "${ISCC_VERSIONS[@]}"; do
    IFS='|' read -r label _ <<< "$entry"
    local out="$dir/smoke-$label.exe" result
    if ! command -v "iscc-$label" > /dev/null; then
      echo "iscc-$label: MISSING"; status=1; continue
    fi
    if (cd "$dir" && "iscc-$label" /Q "/Fsmoke-$label" smoke.iss) && [ -f "$out" ]; then
      result="compiled"
      innoextract --list --silent "$out" > /dev/null 2>&1 && result+=", innoextract ok" || result+=", innoextract FAIL"
      innoextract-master --list --silent "$out" > /dev/null 2>&1 && result+=", innoextract-master ok" || result+=", innoextract-master FAIL"
    else
      result="COMPILE FAILED"; status=1
    fi
    echo "iscc-$label: $result"
  done
  return $status
}

case "${1:-}" in
  packages) step_packages ;;
  sevenzip) step_sevenzip ;;
  iscc) step_iscc ;;
  old) step_old ;;
  sources) step_sources ;;
  innoextract) step_innoextract ;;
  smoke) step_smoke ;;
  *) echo "usage: $0 packages|sevenzip|iscc|old|sources|innoextract|smoke" >&2; exit 2 ;;
esac
