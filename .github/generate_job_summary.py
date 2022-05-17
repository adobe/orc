import sys
import json

if __name__ == "__main__":
    sys.stdout = open(sys.argv[1], "w")
    github = json.load(open(sys.argv[2], "r"))
    job = json.load(open(sys.argv[3], "r"))
    steps = json.load(open(sys.argv[4], "r"))
    runner = json.load(open(sys.argv[5], "r"))
    strategy = json.load(open(sys.argv[6], "r"))
    matrix = json.load(open(sys.argv[7], "r"))

    print(f"# {github['workflow']} Summary")
    print("")

    print("## Details")
    print(f"- started by: `{github['actor']}`")
    print(f"- branch: `{github['ref']}`")

    print("")

    print("## Summary of Steps")
    print("| Run | Result | Notes |")
    print("|---|---|---|")

    for key in steps:
        value = steps[key];
        outcome = value['outcome']
        outcome_emoji = ":green_circle:" if outcome == 'success' else ":red_circle:"
        print(f"| {key} | {outcome_emoji} {outcome} | :rocket: |")
