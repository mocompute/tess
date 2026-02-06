---
description: Generate or update changelog entries in NEWS.md following the Keep a Changelog format
---

Generate a changelog entry for the Tess compiler project and add it to `NEWS.md`.

## Commit Range

The commit range is determined automatically:

- **Start commit** (last commit in NEWS.md): !`grep -m1 '^\#\# ' NEWS.md | sed 's/.*\.\.\([a-f0-9]*\)).*/\1/'`
- **End commit** (current HEAD of main): !`git rev-parse --short main`

If the start and end commits are the same, tell the user there are no new commits to document.

## Process

1. **Research git commits**: Launch a `general-purpose` agent to analyze commits over the range determined above. Have the agent examine both commit messages AND actual diffs to understand what changed. Use this agent prompt (substituting the actual commit hashes):

   ```
   Research the git commits from <start-commit>..<end-commit> in this repository.
   Look at the commit messages and the actual code changes to understand what work
   has been done during that period. Identify broad themes and categorize them into
   these sections: Added, Changed, Deprecated, Removed, Fixed, Security.

   Focus on:
   1. New features or functionality added
   2. Bug fixes and improvements
   3. Changes to existing behavior
   4. Removed functionality
   5. Breaking changes

   Don't list every commit - synthesize the work into high-level themes and notable
   changes. Look at the actual diffs to understand what really changed beyond just
   commit messages.
   ```

2. **Synthesize into high-level themes**:
   - Don't list individual commits - group related work into themes
   - Example: Instead of listing 10 commits about MSVC fixes, write "Fixed extensive Windows/MSVC compatibility issues (22+ commits): struct alignment, thread-local storage, temp file handling..."
   - Combine related small changes into single bullets

3. **Write in user-friendly language**:
   - Avoid technical jargon, error codes, or internal implementation details
   - Example: Use "incorrect use of temporary return values" instead of "MSVC C2102 errors"
   - Focus on what changed and why it matters, not how it was implemented
   - Describe features from a user's perspective

4. **Create the Highlights section**:
   - After categorizing all changes, identify the 3-5 most significant ones
   - These should be the changes that would most interest someone scanning the changelog
   - Include major features, breaking changes, significant improvements, and milestone achievements
   - Keep highlights concise - just the key point, not full details

5. **Add to NEWS.md**:
   - Add the new section at the top of the file (after the header)
   - Most recent changes should appear first

## Entry Format

Use ISO 8601 dates (YYYY-MM-DD). Each entry follows this structure:

```markdown
## [Unreleased] - YYYY-MM-DD to YYYY-MM-DD (git commit range)

### Highlights

- 3-5 bullet points summarizing the most important changes
- Focus on major features, breaking changes, and significant improvements
- Keep bullets concise and high-level

### Added
### Changed
### Deprecated
### Removed
### Fixed
### Security
```

**Section Guidelines**:
- **Highlights**: The 3-5 most significant changes from this period. This is what readers scan first.
- **Added**: New features, functionality, documentation, or tools that didn't exist before.
- **Changed**: Modifications to existing functionality, refactoring, performance improvements, or behavior changes.
- **Deprecated**: Features marked for future removal (include migration guidance if applicable).
- **Removed**: Features, code, or dependencies that were deleted.
- **Fixed**: Bug fixes, compilation errors, memory issues, or correctness improvements.
- **Security**: Security-related fixes or improvements (if any).

## Example Entry

```markdown
## [Unreleased] - 2026-01-28 to 2026-01-30 (660c97f..576c968)

### Highlights

- New code formatter with sophisticated formatting capabilities
- Comprehensive array library with functional programming operations
- Attributes system for function and parameter annotations
- Arity-based function overloading

### Added

- **Code Formatter**: New `tess fmt` subcommand with multi-line token alignment...
- **Array Standard Library**: Comprehensive Array.tl API with element access...

### Changed

- **Name Mangling**: Changed separator from single underscore to double underscore...

### Removed

- **Positional Struct Initialization**: Removed support for positional struct...

### Fixed

- Fixed MSVC misoptimization of small string writes through `str_span` alias.
- Fixed specialization issues with lambda arguments...
```

## Writing Tips

- **Group related changes**: If multiple commits address the same feature or bug, combine them into a single bullet
- **Be specific but concise**: Include enough detail to understand what changed, but don't write paragraphs
- **Use bold for major topics**: Start bullets with `**Topic Name**:` for important additions or changes
- **Quantify when helpful**: Mention counts like "60+ new tests" or "22+ commits" to show scope
- **Breaking changes are important**: Always highlight breaking changes prominently in the Highlights section
- **Empty sections are OK**: Omit sections with no entries
