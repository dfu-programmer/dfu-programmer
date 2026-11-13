# Releasing dfu-programmer

Releases are built and published by GitHub Actions (`.github/workflows/build.yml`) when a `vX.Y.Z` tag is pushed.

## Before tagging

1. Update `NEWS`. The release notes are taken from its first section, up to the previous `== Release` line.
2. Update `AUTHORS`, `README.md`, and the man page (`docs/dfu-programmer.1` and `docs/dfu-programmer.html`) if they're out of date.
   `dfu-programmer --targets-tex` and `--targets-html` print the supported chips lists for the man page.
3. Bump the version in `AC_INIT` in `configure.ac`, and add a `%changelog` entry to `dfu-programmer.spec.in`.
4. Check the latest `master` run of the [Build workflow](https://github.com/dfu-programmer/dfu-programmer/actions/workflows/build.yml) passed.
   Its `release-preview` artifact holds exactly what the release will publish: download it and try the binaries.

## Tag

```bash
git tag -a vX.Y.Z -m "dfu-programmer X.Y.Z"
git push origin vX.Y.Z
```

The tag's run builds every platform and publishes a GitHub release with:

- the source tarball (`dfu-programmer-X.Y.Z.tar.gz`), the one packagers use;
- self-contained binaries: Windows x64 and arm64, Linux x64 and arm64, and a macOS universal (x64 + arm64) binary, each as `.zip` and `.7z`;
- `SHA256SUMS`;
- a [build provenance attestation](https://docs.github.com/en/actions/security-for-github-actions/using-artifact-attestations) for every file. Check one with `gh attestation verify <file> --repo dfu-programmer/dfu-programmer`.

If the release run fails, fix the problem on `master`, then delete the tag (and the draft release, if one was made) and tag again.

## After the release

- Update the [website](https://github.com/dfu-programmer/dfu-programmer.github.io).
- Downstream packages pick up the new tarball on their own schedules. Homebrew, MacPorts, Arch, Debian, MSYS2 and Scoop all package dfu-programmer.
