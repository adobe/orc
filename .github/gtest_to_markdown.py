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

def format_failure_message(failure: Dict[str, Any]) -> str:
    """
    Format a test failure message from the gtest JSON output.

    Args:
        failure (Dict[str, Any]): A dictionary containing failure information
            with optional keys:
            - message: The failure message
            - type: The type of failure

    Returns:
        str: Formatted failure message combining the message and type
            if both are present, otherwise just the message.

    Example:
        >>> failure = {"message": "Expected 2 but got 3", "type": "AssertionError"}
        >>> format_failure_message(failure)
        'Expected 2 but got 3\nType: AssertionError'
    """
    message = []
    if "message" in failure:
        message.append(failure["message"])
    if "type" in failure:
        message.append(f"Type: {failure['type']}")
    return "\n".join(message)

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