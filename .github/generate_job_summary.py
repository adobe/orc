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
    if "event" in github:
        event = github['event']
        if "pull_request" in event:
            print(f"- branch: `{event['pull_request']['head']['ref']}`")
        if "action" in event:
            print(f"- action: `{event['action']}`")

    print("")

    print("## Summary of Steps")
    print("| Run | Result | Notes |")
    print("|---|---|---|")

    all_success = True

    for key in steps:
        value = steps[key];
        outcome = value['outcome']
        cur_success = outcome == 'success'
        all_success &= cur_success
        outcome_emoji = ":green_circle:" if cur_success else ":red_circle:"
        outputs = value['outputs']
        if outputs == {}:
            outputs = ""
        else:
            outputs_string = "<ol>"
            for key in outputs:
                outputs_string += f"<li>{key}: {outputs[key]}</li>"
            outputs_string = "</ol>"
            outputs = outputs_string
        print(f"| {key} | {outcome_emoji} {outcome} | {outputs} |")

    print("<code>")
    print(json.dumps(steps, indent=4, sort_keys=False))
    print("</code>")

    if not all_success:
        sys.exit("One or more tests failed")
