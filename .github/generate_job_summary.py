import sys
import json

if __name__ == "__main__":
    print(f"Arguments count: {len(sys.argv)}")
    for i, arg in enumerate(sys.argv):
        print(f"Argument {i:>6}: {arg}")

    sys.stdout = open(sys.argv[1], "w")

    github = json.load(open(sys.argv[2], "r"))
    job = json.load(open(sys.argv[3], "r"))
    steps = json.load(open(sys.argv[4], "r"))
    runner = json.load(open(sys.argv[5], "r"))
    strategy = json.load(open(sys.argv[6], "r"))
    matrix = json.load(open(sys.argv[7], "r"))

    print("# Job Summary")
    print("| Run | Result | Notes |")
    print("|---|---|---|")

    for key in steps:
        value = steps[key];
        print(f"| {key} | {value} | :rocket: |")
