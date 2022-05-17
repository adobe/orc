import sys
import json

if __name__ == "__main__":
    print(f"Arguments count: {len(sys.argv)}")
    for i, arg in enumerate(sys.argv):
        print(f"Argument {i:>6}: {arg}")

    sys.stdout = open(sys.argv[1], "w")

    github = json.load(sys.argv[2])
    job = json.load(sys.argv[3])
    steps = json.load(sys.argv[4])
    runner = json.load(sys.argv[5])
    strategy = json.load(sys.argv[6])
    matrix = json.load(sys.argv[7])

    print("# Job Summary")
    print("| Run | Result | Notes |")
    print("|---|---|---|")

    for key in steps:
        value = steps[key];
        print(f"| {key} | {value.outcome} | :rocket: |")
