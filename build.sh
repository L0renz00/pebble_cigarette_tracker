#!/bin/bash
pebble clean && \
pebble build -v > build.log 2>&1 &&\
pebble install --emulator basalt && \
pebble install --emulator emery
