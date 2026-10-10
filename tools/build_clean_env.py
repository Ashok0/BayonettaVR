"""Run MSBuild after removing case-duplicate environment variable names."""

import os
import subprocess
import sys


def main() -> int:
    environment = {}
    seen = set()
    # Preserve the normally-cased Path value when the host supplies both PATH
    # and Path; .NET Framework rejects such a process environment.
    items = sorted(os.environ.items(), key=lambda item: 0 if item[0] == "Path" else 1)
    for key, value in items:
        folded = key.lower()
        if folded not in seen:
            environment[key] = value
            seen.add(folded)

    msbuild = (
        r"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild"
        r"\Current\Bin\MSBuild.exe"
    )
    return subprocess.call(
        [
            msbuild,
            "BayonettaVR.vcxproj",
            "/t:Build",
            "/p:Configuration=Release",
            "/p:Platform=Win32",
            "/p:DeployToGame=false",
        ],
        env=environment,
    )


if __name__ == "__main__":
    sys.exit(main())
