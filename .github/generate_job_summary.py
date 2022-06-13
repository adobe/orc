import sys
import json

if __name__ == "__main__":
    sys.stdout = open(sys.argv[1], "w")
    github = json.load(open(sys.argv[2], "r"))
    steps = json.load(open(sys.argv[3], "r"))

    print(f"# {github['workflow']}: Job Summary")
    print("")

    print("## Details")
    print(f"- started by: `{github['actor']}`")
    if "event" in github and "pull_request" in github['event']:
        print(f"- branch: `{github['event']['pull_request']['head']['ref']}`")
    print(f"- action: `{github['event']['action']}`")

    print("")

    print("## Summary of Steps")
    print("| Run | Result | Notes |")
    print("|---|---|---|")

    failure = False

    for key in steps:
        value = steps[key];
        outcome = value['outcome']
        failure |= outcome == 'success'
        outcome_emoji = ":green_circle:" if outcome == 'success' else ":red_circle:"
        outputs = value['outputs']
        print(f"| {key} | {outcome_emoji} {outcome} | {outputs} |")

    if failure:
        sys.exit("One or more tests failed")
