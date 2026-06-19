#!/bin/bash
# Run Go <-> C++ interop tests
cd "$(dirname "$0")/../go"
go run ./cmd/interop
