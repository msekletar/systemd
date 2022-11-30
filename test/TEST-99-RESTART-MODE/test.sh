#!/usr/bin/env bash
set -e

TEST_DESCRIPTION="Test for RestartMode= feature"

# shellcheck source=test/test-functions
. "${TEST_BASE_DIR:?}/test-functions"

do_test "$@"
