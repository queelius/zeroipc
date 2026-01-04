#!/bin/bash
# Run Go <-> C++ interop tests
cd "$(dirname "$0")/../go"
go run ../interop/test_go_cpp_interop.go
