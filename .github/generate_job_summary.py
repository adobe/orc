import sys

if __name__ == "__main__":
    print(f"Arguments count: {len(sys.argv)}")
    for i, arg in enumerate(sys.argv):
        print(f"Argument {i:>6}: {arg}")

    f = open(sys.argv[1], "w")

    f.write("# Job Summary")
    f.write("| Run | Result | Notes |")
    f.write("|---|---|---|")
    f.write("| Hello | world! | :rocket: |")
