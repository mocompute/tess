#!/bin/bash
# Complete test suite for tl-mode

echo "=== TL Mode Complete Test Suite ==="
echo ""

echo "1. Running unit tests..."
emacs --batch -l test-tl-mode.el 2>&1 | grep -E "^(Test|✓|✗|===)"

echo ""
echo "2. Running final comprehensive tests..."
emacs --batch -l final-test.el 2>&1 | grep -v "^Loading" | grep -E "^(Test|✓|✗|===|  )"

echo ""
echo "3. Files delivered:"
ls -lh *.el *.md *.tl 2>/dev/null | awk '{print "  ", $9, "("$5")"}'

echo ""
echo "=== Test Suite Complete ==="
