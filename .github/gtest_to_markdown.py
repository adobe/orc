#!/usr/bin/env python3

import json
import sys
from datetime import datetime
from typing import Dict, List, Any

def format_duration(milliseconds: float) -> str:
    """Convert milliseconds to a human-readable duration."""
    if milliseconds < 1000:
        return f"{milliseconds:.2f}ms"
    seconds = milliseconds / 1000
    if seconds < 60:
        return f"{seconds:.2f}s"
    minutes = seconds / 60
    return f"{minutes:.2f}m"

def format_failure_message(failure: Dict[str, Any]) -> str:
    """Format a test failure message."""
    message = []
    if "message" in failure:
        message.append(failure["message"])
    if "type" in failure:
        message.append(f"Type: {failure['type']}")
    return "\n".join(message)

def convert_to_markdown(data: Dict[str, Any]) -> str:
    """Convert gtest JSON data to GitHub-flavored markdown."""
    output = []
    
    # Add header with timestamp
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    output.append(f"# GTest Results ({timestamp})\n")
    
    # Add summary
    total_tests = len(data.get("testsuites", []))
    passed_tests = sum(1 for suite in data.get("testsuites", [])
                      if suite.get("status") == "RUN")
    failed_tests = total_tests - passed_tests
    
    output.append("## Summary\n")
    output.append(f"- Total Tests: {total_tests}")
    output.append(f"- Passed: {passed_tests}")
    output.append(f"- Failed: {failed_tests}\n")
    
    # Add detailed results table
    output.append("## Test Results\n")
    output.append("| Test Suite | Test Case | Status | Duration |")
    output.append("|------------|-----------|--------|----------|")
    
    for suite in data.get("testsuites", []):
        suite_name = suite.get("name", "Unknown Suite")
        for test in suite.get("testsuite", []):
            test_name = test.get("name", "Unknown Test")
            status = "✅ PASS" if test.get("status") == "RUN" else "❌ FAIL"
            time_value = test.get("time", 0)
            try:
                # Try to convert to float, but handle string values like "0s"
                if isinstance(time_value, str) and time_value.endswith('s'):
                    duration_ms = float(time_value[:-1]) * 1000  # Convert seconds to milliseconds
                else:
                    duration_ms = float(time_value)
                duration = format_duration(duration_ms)
            except ValueError:
                # Fallback if conversion fails
                duration = str(time_value)
            
            # Add the test result row
            output.append(f"| {suite_name} | {test_name} | {status} | {duration} |")
            
            # Add failure details if the test failed
            if test.get("status") != "RUN" and "failures" in test:
                output.append("\n### Failure Details\n")
                for failure in test["failures"]:
                    output.append("```")
                    output.append(format_failure_message(failure))
                    output.append("```\n")
    
    return "\n".join(output)

def main():
    if len(sys.argv) != 2:
        print("Usage: python gtest_to_markdown.py <gtest_json_file>")
        sys.exit(1)
    
    try:
        with open(sys.argv[1], 'r') as f:
            data = json.load(f)
        markdown = convert_to_markdown(data)
        print(markdown)
    except FileNotFoundError:
        print(f"Error: File {sys.argv[1]} not found", file=sys.stderr)
        sys.exit(1)
    except json.JSONDecodeError:
        print(f"Error: Invalid JSON in {sys.argv[1]}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Error: {str(e)}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main() 