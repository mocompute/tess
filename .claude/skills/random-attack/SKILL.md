---
name: random-attack
description: "Picks a random recent git commit and adversarially tests it against the Tess compiler. Selects a commit from a configurable time period (default: 24 hours), analyzes the code changes, crafts a .tl test that tries to expose bugs, runs it, and places the test in the correct test directory. Invoke with /random-attack [time-period]. Use this skill whenever the user wants to fuzz recent changes, randomly stress-test the compiler, or run adversarial testing on recent work."
---

# Random Attack

Pick a random recent commit and run an adversarial test against it.

## Usage

- `/random-attack` — random commit from the last 24 hours
- `/random-attack 7 days` — random commit from the last 7 days
- `/random-attack 3 hours` — random commit from the last 3 hours

Any time expression that `git log --since` accepts works.

## Step 1: Select a random commit

```bash
git log --since="<time_period> ago" --format="%H %s" | shuf -n 1
```

Default time period is `24 hours` if the user doesn't specify.

If no commits are found, tell the user and suggest a wider range. Show how many commits are in the range so the user has context.

**Skip trivial commits.** Before proceeding, check the commit's diff with `git show <hash> --stat`. Re-roll (pick another random commit) if the commit is:
- Formatting/whitespace only (commit messages like "formatting", "apply formatters", "style")
- Documentation only (only `.md` files changed)
- Pure dead code removal (only deletions, no new logic — messages like "remove unused...", "delete dead code")
- Fewer than 3 lines of non-comment, non-whitespace code changed

To check, run `git diff <hash>~1 <hash> --stat` and glance at the diff. If it's clearly cosmetic, pick again. After 3 re-rolls with no substantive commit found, tell the user and suggest a wider time range.

Display the selected commit (short hash + message) before proceeding.

## Step 2: Run the attack

Read the attack methodology at `.claude/skills/attack/references/attack-methodology.md` and follow the full procedure against the selected commit.
