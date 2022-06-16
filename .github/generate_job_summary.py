import sys
import json
import re

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
    print("| Step | Test | Notes | Expected | Reported |")
    print("|---|---|---|---|---|")

    all_success = True
    p = re.compile('(?<!\\\\)\'')

    for key in steps:
        value = steps[key];
        outcome = value['outcome']
        cur_success = outcome == 'success'
        all_success &= cur_success
        outcome_emoji = ":green_circle:" if cur_success else ":red_circle:"
        outputs = value['outputs']
        if outputs == {}:
            print(f"| **{key}** | | {outcome_emoji} {outcome} | | |")
        else:
            if "orc_test_out" in outputs:
                print(f"| **{key}** | | {outcome_emoji} {outcome} | | |")
                outputs = p.sub('\"', outputs["orc_test_out"])
                outputs = json.loads(outputs)
                for run in outputs:
                    if isinstance(outputs[run], list):
                        continue
                    expected = outputs[run]["expected"]
                    reported = outputs[run]["reported"]
                    outcome_emoji = ":green_circle: success" if expected == reported else ":red_circle: failure"
                    print(f"| | `{run}` | {outcome_emoji} | {expected} | {reported} |")
            else:
                print(f"| **{key}** | | {outcome_emoji} {outcome} {outputs} | | |")

    # Keep these for debugging; they can be used to serialize the various
    # environment variables available to us through GitHub Actions. Be
    # careful, though, not to leave these in production: they produce
    # copious (and possibly sensitive) output.

    if False:
        print("## github")
        print("<code>")
        print(json.dumps(github, indent=4, sort_keys=False))
        print("</code>")

        print("## steps")
        print("<code>")
        print(json.dumps(steps, indent=4, sort_keys=False))
        print("</code>")

    if not all_success:
        sys.exit("One or more tests failed")
