# mdpdf

A small C CLI that converts Markdown to PDF.

- Uses only standard PDF fonts (Helvetica for body text, Courier for code). No font embedding; output stays small
- Detects paper size automatically (`en_US`/`en_CA` use Letter, otherwise A4). Override with `PAPERSIZE` or `/etc/papersize`
- Renders headings, paragraphs, inline bold/italic/code, fenced and indented code blocks, unordered and ordered lists, blockquotes, horizontal rules, and images (JPEG/PNG)
- Depends only on `zlib` (for PNG)

## Building

```sh
make
```

One `make` call builds the tool. GCC or Clang with a C11-capable
standard library and `zlib-dev` (or equivalent) is required.

To install system-wide:

```sh
sudo make install   # installs to /usr/local/bin/
```

## Usage

```sh
mdpdf input.md            # writes input.pdf
mdpdf input.md output.pdf # explicit output path
```

## Supported Markdown

| Feature | Example |
|---------|---------|
| ATX headings | `# H1` … `###### H6` |
| Setext headings | `Title\n===` |
| Bold | `**bold**` or `__bold__` |
| Italic | `*italic*` or `_italic_` |
| Bold-italic | `***both***` |
| Inline code | `` `code` `` |
| Fenced code block | ` ```lang ` |
| Indented code block | 4-space or tab indent |
| Unordered list | `- item` |
| Ordered list | `1. item` |
| Blockquote | `> text` |
| Horizontal rule | `---` |
| Image | `![alt](path.jpg)` |
| Link | `[text](url)` (text rendered in blue, clickable hyperlink) |

Tables are not supported.

### Images

JPEG and PNG are supported. The path in `![alt](path)` is
resolved relative to the Markdown file directory.

JPEG images are embedded directly (DCTDecode). PNG images are decoded
using zlib and the raw pixels are re-compressed with FlateDecode.
