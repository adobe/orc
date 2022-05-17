import sys
import json

if __name__ == "__main__":
    print(f"Arguments count: {len(sys.argv)}")
    for i, arg in enumerate(sys.argv):
        print(f"Argument {i:>6}: {arg}")

    original_stdout = sys.stdout

    f = open(sys.argv[1], "w")

    sys.stdout = f

    print("# Job Summary")
    print("| Run | Result | Notes |")
    print("|---|---|---|")
    print("| Hello | world! | :rocket: |")
