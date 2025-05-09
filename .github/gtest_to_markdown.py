#!/usr/bin/env python3

"""
Google Test to Markdown Converter

This script converts Google Test (gtest) JSON output into GitHub-flavored markdown format.
It processes test results and generates a well-formatted markdown document that includes:
- A timestamped header
- Summary statistics (total, passed, and failed tests)
- A detailed table of test results
- Failure details for any failed tests

The script is designed to be used as a command-line tool, taking two arguments:
1. context: A string describing the test run context
2. gtest_json_file: Path to the JSON file containing gtest results

Example usage:
    python gtest_to_markdown.py "Unit Tests" test_results.json

The output is formatted markdown that can be directly used in GitHub issues, pull requests,
or documentation.
"""

import json
import os
import sys
from datetime import datetime
from typing import Dict, List, Any

def format_duration(milliseconds: float) -> str:
    """
    Convert milliseconds to a human-readable duration string.

    Args:
        milliseconds (float): Duration in milliseconds

    Returns:
        str: Human-readable duration string in the format:
            - "X.XXms" for durations < 1 second
            - "X.XXs" for durations < 1 minute
            - "X.XXm" for durations >= 1 minute

    Example:
        >>> format_duration(500)
        '500.00ms'
        >>> format_duration(1500)
        '1.50s'
    """
    if milliseconds < 1000:
        return f"{milliseconds:.2f}ms"
    seconds = milliseconds / 1000
    if seconds < 60:
        return f"{seconds:.2f}s"
    minutes = seconds / 60
    return f"{minutes:.2f}m"

def parse_duration(time_value: Any) -> float:
    """
    Parse a duration value from gtest output into milliseconds.

    Args:
        time_value (Any): The duration value from gtest, which could be:
            - A float (milliseconds)
            - A string ending in 's' (seconds)
            - Any other value that should be converted to float

    Returns:
        float: Duration in milliseconds

    Example:
        >>> parse_duration(500)
        500.0
        >>> parse_duration("1.5s")
        1500.0
    """
    try:
        if isinstance(time_value, str) and time_value.endswith('s'):
            return float(time_value[:-1]) * 1000  # Convert seconds to milliseconds
        return float(time_value)
    except (ValueError, TypeError):
        return 0.0  # Return 0 for invalid values

def format_failure_message(failure: Dict[str, Any]) -> str:
    """
    Format a test failure message from the gtest JSON output.

    Args:
        failure (Dict[str, Any]): A dictionary containing failure information
            with optional keys:
            - failure: The failure message
            - type: The type of failure

    Returns:
        str: The message with all newlines replaced with "<br/>"

    Example:
        >>> failure = {"failure": "Expected 2\nbut got 3"}
        >>> format_failure_message(failure)
        'Expected 2<br/>but got 3'
    """
    if "failure" in failure:
        return "<pre>" + failure["failure"].replace("\n", "<br/>") + "</pre>"
    return ""

def convert_to_markdown(data: Dict[str, Any], context: str) -> str:
    """
    Convert gtest JSON data to GitHub-flavored markdown.

    This function processes the gtest JSON output and generates a comprehensive
    markdown document that includes test results, statistics, and failure details.

    Args:
        data (Dict[str, Any]): The parsed JSON data from gtest output
        context (str): A string describing the context of the test run
            (e.g., "Unit Tests", "Integration Tests")

    Returns:
        str: A complete markdown document containing:
            - Header with timestamp
            - Summary statistics
            - Detailed test results table
            - Failure details for failed tests

    The output markdown includes:
        - A table with columns: Test Suite, Test Case, Status, Duration
        - Emoji indicators (✅ for pass, ❌ for fail)
        - Formatted duration strings
        - Code blocks for failure messages
    """
    output = []
    
    # Add header with timestamp
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    output.append(f"# {context} Results ({timestamp})\n")
    
    # Add summary
    total_tests = data.get("tests", 0)
    failed_tests = data.get("failures", 0)
    disabled_tests = data.get("disabled", 0)
    error_tests = data.get("errors", 0)
    tests_duration = format_duration(parse_duration(data.get("time", 0)))
    passed_tests = total_tests - failed_tests
    
    output.append("## Summary\n")
    output.append(f"- Tests: {total_tests}")
    output.append(f"- Passed: {passed_tests}")
    output.append(f"- Failed: {failed_tests}")
    output.append(f"- Disabled: {disabled_tests}")
    output.append(f"- Errors: {error_tests}")
    output.append(f"- Duration: {tests_duration}\n")
    
    # Add detailed results table
    output.append("## Details\n")
    output.append("| Suite | Case | Status | Duration | Details |")
    output.append("|-------|------|--------|----------|---------|")
    
    for suite in data.get("testsuites", []):
        suite_name = suite.get("name", "Unknown Suite")
        for test in suite.get("testsuite", []):
            test_name = test.get("name", "Unknown Test")
            status = "❌ FAIL" if "failures" in test else "✅ PASS"
            duration = format_duration(parse_duration(test.get("time", 0)))
            details = []

            # Add failure details if the test failed
            if "failures" in test:
                for failure in test["failures"]:
                    details.append(format_failure_message(failure))

            # Add the test result row
            output.append(f"| {suite_name} | {test_name} | {status} | {duration} | {'<br/>'.join(details)}")
    
    return "\n".join(output)

def main():
    """
    Main entry point for the script.

    Processes command line arguments and converts gtest JSON output to markdown.
    The script expects two arguments:
    1. context: A string describing the test run context
    2. gtest_json_file: Path to the JSON file containing gtest results

    The script will:
    - Read and parse the JSON file
    - Convert the data to markdown format
    - Print the markdown to stdout

    Exits with status code 1 if:
    - Incorrect number of arguments
    - File not found
    - Invalid JSON
    - Any other error occurs
    """
    if len(sys.argv) != 3:
        print("Usage: python gtest_to_markdown.py <context> <gtest_json_file>")
        sys.exit(1)
    
    context = sys.argv[1]
    json_file = sys.argv[2]
    
    try:
        with open(json_file, 'r') as f:
            data = json.load(f)
        markdown = convert_to_markdown(data, context)
        print(markdown)
    except FileNotFoundError:
        print(f"Error: File {json_file} not found", file=sys.stderr)
        sys.exit(1)
    except json.JSONDecodeError:
        print(f"Error: Invalid JSON in {json_file}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Error: {str(e)}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main() 