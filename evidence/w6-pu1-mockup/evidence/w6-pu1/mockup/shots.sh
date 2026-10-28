#!/bin/bash
cd "$(dirname "$0")"
for t in light dark highContrastWhite highContrastBlack; do
  for m in doc home; do ./body.sh $t shots $m; done
done
ls -la shots
