import sys
import json

if __name__ == "__main__":
    test_results = json.load(open(sys.argv[1], "r"))
    print(f"::set-output name=orc_test_out::{test_results}");
