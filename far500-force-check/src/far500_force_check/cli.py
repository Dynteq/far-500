"""CLI: python -m far500_force_check <input> -o <rapport.xlsx> [--h-thresh ...]"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from dataclasses import fields
from pathlib import Path
from typing import Optional

from .constants import Criteria
from .engine import analyze
from .parser import ParseError, load_recording
from .xlsx_writer import build_workbook


def _str2bool(v: str) -> bool:
    return v.strip().lower() in ("1", "true", "aan", "yes", "ja")


def _build_arg_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="far500-force-check",
        description=(
            "Toets een FAR-500-meetexport (CSV/XLSX) aan de bedienkracht-criteria "
            "C1-C4 en genereer een XLSX-rapport (setup_analyse + data)."
        ),
    )
    p.add_argument("input", type=Path, help="pad naar de meting-export (.csv of .xlsx)")
    p.add_argument(
        "-o", "--output", type=Path, default=None,
        help="pad voor het XLSX-rapport (default: <input>_rapport.xlsx)",
    )
    p.add_argument(
        "--print-only", type=Path, default=None,
        help="schrijf ook een apart bronbestand met alleen het tabblad setup_analyse (voor PDF-conversie)",
    )
    p.add_argument(
        "--pdf", action="store_true",
        help="render --print-only ook naar PDF via LibreOffice ('soffice'), als dat op PATH staat",
    )

    for f in fields(Criteria):
        flag = "--" + f.name.lower().replace("_", "-")
        if f.type is bool:
            p.add_argument(flag, type=_str2bool, default=None, help=f"override voor {f.name} (default {f.default})")
        else:
            p.add_argument(flag, type=float, default=None, help=f"override voor {f.name} (default {f.default})")
    return p


def _criteria_from_args(args: argparse.Namespace) -> Criteria:
    overrides = {}
    for f in fields(Criteria):
        val = getattr(args, f.name.lower(), None)
        if val is not None:
            overrides[f.name] = val
    return Criteria(**overrides)


def main(argv: Optional[list[str]] = None) -> int:
    args = _build_arg_parser().parse_args(argv)
    criteria = _criteria_from_args(args)

    try:
        recording = load_recording(args.input)
    except ParseError as e:
        print(f"Fout bij het inlezen van '{args.input}': {e}", file=sys.stderr)
        return 2

    analysis = analyze(recording, criteria)

    output = args.output or args.input.with_name(args.input.stem + "_rapport.xlsx")
    build_workbook(analysis, include_data_tab=True).save(output)
    print(f"Rapport geschreven naar {output} — eindoordeel: {'PASS' if analysis.overall_pass else 'FAIL'}")

    if args.print_only:
        build_workbook(analysis, include_data_tab=False).save(args.print_only)
        print(f"Print-only workbook (alleen setup_analyse) geschreven naar {args.print_only}")

        if args.pdf:
            soffice = shutil.which("soffice")
            if not soffice:
                print(
                    "Waarschuwing: 'soffice' (LibreOffice) niet gevonden op PATH — PDF-stap overgeslagen.",
                    file=sys.stderr,
                )
            else:
                outdir = args.print_only.parent
                subprocess.run(
                    [
                        soffice, "--headless", "--norestore",
                        "--convert-to", "pdf", "--outdir", str(outdir), str(args.print_only),
                    ],
                    check=True,
                )
                print(f"PDF geschreven naar {outdir / (args.print_only.stem + '.pdf')}")

    return 0 if analysis.overall_pass else 1


if __name__ == "__main__":
    sys.exit(main())
