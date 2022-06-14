import sys
import json

if __name__ == "__main__":
    step_name = sys.argv[1];
    test_results = json.load(open(sys.argv[2], "r"))
    print(f"::set-output step_name::{test_results}");
