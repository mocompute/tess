---
name: attack
description: "Adversarial testing for the Tess compiler targeting a specific git commit. Analyzes the commit's code changes, crafts a .tl test program designed to expose edge cases or bugs, runs it, and places the test in the correct test directory (pass/, fail/, known_failures/, or known_fail_failures/). Invoke with /attack <commit>. Use this skill whenever the user wants to adversarially test a specific commit, stress-test a particular change, or write an edge-case test for a known piece of code."
---

# Attack

Analyze a specific commit and write an adversarial .tl test that tries to break the changed code.

## Usage

The user provides a commit reference (hash, short hash, HEAD~N, branch name, etc.):

```
/attack abc1234
/attack HEAD~3
/attack feature-branch
```

## Procedure

1. **Verify the commit exists**: run `git show <commit> --oneline --stat` to confirm it's valid and see what changed.

2. **Read the attack methodology**: read `references/attack-methodology.md` (relative to this skill's directory) and follow the full procedure:
   - Analyze the commit diff
   - Devise three adversarial attacks
   - Randomly pick one
   - Write the test
   - Run it with `./tess run`
   - Place it in the correct test directory
   - Report results

3. **Ask before committing**: after placing the test, show the user the result but do not commit. Let them decide whether to keep the test, modify it, or discard it.
