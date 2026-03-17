# Word Count

A command-line word counting tool demonstrating several standard library
modules: CommandLine, File, HashMap, Array, and String.

## Features

- Count lines, words, characters, and unique words across multiple files
- Case-insensitive unique word tracking
- Sort output by word count (descending) with optional top-N limit
- Read from stdin when no files are given
- Error handling with tagged unions (`Result`, `when`/`else`)

## Building and running

Compile to an executable:

```bash
tess exe -o wc wordcount.tl
```

Or run directly:

```bash
tess run wordcount.tl -- [options] [files...]
```

## Options

| Flag | Long | Description |
|------|------|-------------|
| `-l` | `--lines` | Show line count |
| `-w` | `--words` | Show word count |
| `-c` | `--chars` | Show character/byte count |
| `-u` | `--unique-words` | Show unique word count |
| `-i` | `--ignore-case` | Case-insensitive unique words (use with `-u`) |
| `-s` | `--sort` | Sort output by word count (descending) |
| `-t` | `--top N` | Show only top N files |
| `-h` | `--help` | Show help message |

When no count flags are given, the default is `-l -w -c` (lines, words,
characters).

## Usage examples

Count lines, words, and characters in a file:

```bash
tess run wordcount.tl -- file.tl
```

Count unique words (case-insensitive):

```bash
tess run wordcount.tl -- -u -i file.tl
```

Sort multiple files by word count, show top 5:

```bash
tess run wordcount.tl -- -s -t 5 src/*.tl
```

Read from stdin:

```bash
echo "hello world" | tess run wordcount.tl
```

## What it demonstrates

- **CommandLine** — declarative flag/option parsing with short and long names
- **File** — reading files with typed error handling (`NotFound`,
  `PermissionDenied`)
- **HashMap** — tracking unique word frequencies
- **Array** — collecting results, sorting, slicing
- **String** — character iteration, slicing, case conversion
- **Tagged unions** — `when`/`else` pattern matching on `Result` and error
  variants
