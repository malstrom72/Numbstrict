#!/bin/bash
set -e -o pipefail -u
cd "$(dirname "$0")"
pandoc -s -o "Makaron Documentation.html" --metadata title="Makaron Documentation" --include-in-header pandoc.css "Makaron Documentation.md"
