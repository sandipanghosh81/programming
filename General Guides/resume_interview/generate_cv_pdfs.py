"""
CV PDF Generator
─────────────────
Converts the one-sheet and full CV markdown files to professional PDFs.
Uses markdown → HTML → PDF pipeline with custom CSS styling.

Requirements: pip install markdown xhtml2pdf
"""

import os
import sys
from pathlib import Path

try:
    import markdown
    from xhtml2pdf import pisa
except ImportError:
    print("Installing required packages...")
    os.system(f"{sys.executable} -m pip install markdown xhtml2pdf")
    import markdown
    from xhtml2pdf import pisa


# ── CSS Styling ─────────────────────────────────────────────────────

ONE_SHEET_CSS = """
@page {
    size: letter;
    margin: 0.35in 0.45in 0.35in 0.45in;
}
body {
    font-family: Helvetica, Arial, sans-serif;
    font-size: 7.5pt;
    line-height: 1.2;
    color: #1a1a1a;
}
h1 {
    font-size: 16pt;
    font-weight: 700;
    color: #0d1b2a;
    margin-bottom: 0pt;
    margin-top: 0;
    letter-spacing: 1pt;
}
h2 {
    font-size: 9pt;
    font-weight: 700;
    color: #1b3a5c;
    border-bottom: 1pt solid #1b3a5c;
    padding-bottom: 1pt;
    margin-top: 6pt;
    margin-bottom: 2pt;
    text-transform: uppercase;
    letter-spacing: 0.5pt;
}
h3 {
    font-size: 8pt;
    font-weight: 600;
    color: #2c5282;
    margin-top: 4pt;
    margin-bottom: 1pt;
}
p {
    margin-top: 1pt;
    margin-bottom: 2pt;
}
strong {
    color: #0d1b2a;
}
table {
    width: 100%;
    border-collapse: collapse;
    margin-top: 2pt;
    margin-bottom: 3pt;
}
th {
    background-color: #1b3a5c;
    color: white;
    font-size: 6.5pt;
    font-weight: 600;
    padding: 2pt 3pt;
    text-align: left;
}
td {
    font-size: 7pt;
    padding: 1.5pt 3pt;
    border-bottom: 0.5pt solid #e0e0e0;
    vertical-align: top;
}
tr:nth-child(even) td {
    background-color: #f7fafc;
}
hr {
    border: none;
    border-top: 0.5pt solid #cbd5e0;
    margin: 3pt 0;
}
a {
    color: #2c5282;
    text-decoration: none;
}
code {
    font-family: "Courier New", monospace;
    font-size: 6.5pt;
    background-color: #f0f0f0;
    padding: 0pt 2pt;
}
"""

FULL_CV_CSS = """
@page {
    size: letter;
    margin: 0.6in 0.65in 0.6in 0.65in;
}
body {
    font-family: Helvetica, Arial, sans-serif;
    font-size: 9.5pt;
    line-height: 1.4;
    color: #1a1a1a;
}
h1 {
    font-size: 22pt;
    font-weight: 700;
    color: #0d1b2a;
    margin-bottom: 2pt;
    margin-top: 0;
    letter-spacing: 1.5pt;
}
h2 {
    font-size: 12pt;
    font-weight: 700;
    color: #1b3a5c;
    border-bottom: 2pt solid #1b3a5c;
    padding-bottom: 3pt;
    margin-top: 14pt;
    margin-bottom: 6pt;
    text-transform: uppercase;
    letter-spacing: 0.5pt;
}
h3 {
    font-size: 11pt;
    font-weight: 600;
    color: #2c5282;
    margin-top: 10pt;
    margin-bottom: 3pt;
}
h4 {
    font-size: 10pt;
    font-weight: 600;
    color: #2d3748;
    margin-top: 8pt;
    margin-bottom: 2pt;
}
p {
    margin-top: 2pt;
    margin-bottom: 5pt;
}
strong {
    color: #0d1b2a;
}
ul {
    margin-top: 2pt;
    margin-bottom: 4pt;
    padding-left: 14pt;
}
li {
    margin-bottom: 2pt;
}
table {
    width: 100%;
    border-collapse: collapse;
    margin-top: 4pt;
    margin-bottom: 8pt;
}
th {
    background-color: #1b3a5c;
    color: white;
    font-size: 8.5pt;
    font-weight: 600;
    padding: 4pt 6pt;
    text-align: left;
}
td {
    font-size: 9pt;
    padding: 3pt 6pt;
    border-bottom: 0.5pt solid #e0e0e0;
    vertical-align: top;
}
tr:nth-child(even) td {
    background-color: #f7fafc;
}
hr {
    border: none;
    border-top: 1pt solid #cbd5e0;
    margin: 8pt 0;
}
a {
    color: #2c5282;
    text-decoration: none;
}
code {
    font-family: "Courier New", monospace;
    font-size: 8.5pt;
    background-color: #f0f0f0;
    padding: 1pt 3pt;
}
"""


def sanitize_for_latin1(text: str) -> str:
    """Replace Unicode characters that latin-1 can't encode."""
    replacements = {
        "\u2014": "--",       # em dash
        "\u2013": "-",        # en dash
        "\u2018": "'",        # left single quote
        "\u2019": "'",        # right single quote
        "\u201c": '"',        # left double quote
        "\u201d": '"',        # right double quote
        "\u2026": "...",      # ellipsis
        "\u2022": "*",        # bullet
        "\u2192": "->",       # right arrow
        "\u2190": "<-",       # left arrow
        "\u2194": "<->",      # bidirectional arrow
        "\u2713": "[ok]",     # check mark
        "\u2717": "[x]",      # cross mark
        "\u00d7": "x",        # multiplication sign
        "\u00b2": "^2",       # superscript 2
        "\u00b3": "^3",       # superscript 3
        "\u2265": ">=",       # greater than or equal
        "\u2264": "<=",       # less than or equal
        "\u2260": "!=",       # not equal
        "\u221e": "inf",      # infinity
        "\u03b1": "alpha",    # alpha
        "\u03b2": "beta",     # beta
        "\u03b4": "delta",    # delta
        "\u00b5": "u",        # micro sign
        "\u2248": "~=",       # approximately equal
        "\u2205": "{}",       # empty set
        "\u2229": "AND",      # intersection
        "\u222a": "OR",       # union
        "\u2502": "|",        # box drawing
        "\u251c": "|--",      # box drawing
        "\u2514": "`--",      # box drawing
        "\u2500": "-",        # box drawing
        "\u250c": "+--",      # box drawing
        "\u2510": "--+",      # box drawing
        "\u2518": "--'",      # box drawing
        "\u2524": "--|",      # box drawing
        "\u253c": "-+-",      # box drawing
        "\u252c": "-+-",      # box drawing
        "\u2534": "-+-",      # box drawing
        "\u25a0": "[#]",      # black square
        "\u25cb": "(o)",      # white circle
        "\u25cf": "(*)   ",   # black circle
        "\u2588": "#",        # full block
        "\u2591": ".",        # light shade
        "\u2593": "#",        # dark shade
        "\u00bd": "1/2",      # one half
        "\u00bc": "1/4",      # one quarter
        "\u00be": "3/4",      # three quarters
        "\u2153": "1/3",      # one third
        "\u2154": "2/3",      # two thirds
        "\u00b1": "+/-",      # plus-minus
        "\u00a9": "(c)",      # copyright
        "\u00ae": "(R)",      # registered
        "\u2122": "(TM)",     # trademark
        "\u2032": "'",        # prime
        "\u2033": "''",       # double prime
        "\u2044": "/",        # fraction slash
        "\u2212": "-",        # minus sign
        "\u2009": " ",        # thin space
        "\u200b": "",         # zero-width space
        "\u00a0": " ",        # non-breaking space
        "\u2003": " ",        # em space
        "\u2002": " ",        # en space
        "\u20ac": "EUR",      # euro
        "\u00a3": "GBP",      # pound
        "\u00a5": "JPY",      # yen
        "\u2605": "*",        # black star
        "\u2606": "*",        # white star
    }
    for char, repl in replacements.items():
        text = text.replace(char, repl)

    # Final pass: replace any remaining non-latin1 characters
    result = []
    for ch in text:
        try:
            ch.encode("latin-1")
            result.append(ch)
        except UnicodeEncodeError:
            result.append("?")
    return "".join(result)


def md_to_pdf(md_path: str, pdf_path: str, css: str) -> None:
    """Convert a markdown file to PDF with custom CSS."""
    md_text = Path(md_path).read_text(encoding="utf-8")
    md_text = sanitize_for_latin1(md_text)

    html_body = markdown.markdown(
        md_text,
        extensions=["tables", "fenced_code", "toc"],
    )

    # xhtml2pdf needs explicit width hints on <th> to distribute columns
    # properly.  Inject width="..." on the first <th> of every 2-column
    # and 3-column table so the narrow label column stays compact and the
    # wide content column gets the rest of the space.
    import re

    def _add_column_widths(html: str) -> str:
        """Add width attributes to <th> tags for better column sizing."""
        # 3-column tables:  #(5%) | Name(30%) | Desc(65%)
        html = re.sub(
            r'<tr>\s*<th([^>]*)>#</th>\s*<th([^>]*)>',
            r'<tr><th\1 width="5%">#</th><th\2 width="30%">',
            html,
        )
        # 2-column tables where first header is short keyword
        # (Domain|Technologies, Category|Detail, Component|What, Role|Period)
        for label in ("Domain", "Category", "Component", "Role"):
            html = re.sub(
                rf'<th([^>]*)>{label}</th>',
                rf'<th\1 width="22%">{label}</th>',
                html,
            )
        # MCP Servers table: Server(28%) | Capability
        html = re.sub(
            r'<th([^>]*)>Server</th>',
            r'<th\1 width="28%">Server</th>',
            html,
        )
        return html

    html_body = _add_column_widths(html_body)

    full_html = f"""<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8"/>
<style>{css}</style>
</head>
<body>
{html_body}
</body>
</html>"""

    with open(pdf_path, "wb") as f:
        status = pisa.CreatePDF(full_html, dest=f)

    if status.err:
        print(f"  ERROR generating {pdf_path}")
    else:
        size_kb = os.path.getsize(pdf_path) / 1024
        print(f"  OK: {pdf_path} ({size_kb:.0f} KB)")


def main():
    cv_dir = Path(__file__).resolve().parent

    print("Generating PDFs...")
    print()

    # One-sheet
    md_to_pdf(
        str(cv_dir / "sandipan_ghosh_one_sheet.md"),
        str(cv_dir / "Sandipan_Ghosh_One_Sheet.pdf"),
        ONE_SHEET_CSS,
    )

    # Full CV
    md_to_pdf(
        str(cv_dir / "sandipan_ghosh_full_cv.md"),
        str(cv_dir / "Sandipan_Ghosh_Full_CV.pdf"),
        FULL_CV_CSS,
    )

    print()
    print("Done.")


if __name__ == "__main__":
    main()
