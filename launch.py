"""PyInstaller entry point; production behaviour lives in comtoolx.app."""

from comtoolx.app import main

if __name__ == "__main__":
    raise SystemExit(main())
