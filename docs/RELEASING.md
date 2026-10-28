# CI and GitHub releases

CI runs on every push and pull request. It builds, runs the tests, deploys Qt dependencies, launches the deployed application, and uploads downloadable build artifacts for:

| Platform | Runner / architecture | Downloads |
|---|---|---|
| macOS Apple Silicon | macos-14 / arm64 | `.dmg` and `.zip` containing the `.app` |
| macOS Intel | macos-15-intel / x86_64 | `.dmg` and `.zip` containing the `.app` |
| Linux | Ubuntu 22.04 / x86_64 | `.tar.gz` with `AppRun`, executable, Qt libraries and plugins |
| Windows | Windows 2022 / x86_64 | NSIS `-setup.exe` installer and `-portable.zip` containing the executable and DLLs |

Packages are built natively with Qt 6.9.3. SHA-256 checksum files accompany every download. The Linux package targets Ubuntu 22.04 or newer compatible desktop systems and uses their system graphics libraries. Extract the archive and run `./AppRun`; no Qt development installation is required.

macOS bundles use ad-hoc signing for executable integrity, including Apple Silicon. They are not Developer ID signed or notarized. Windows downloads are not Authenticode signed. OS security prompts can therefore appear. Distribution signing identities and notarization credentials have not been configured.

## Publishing a release

1. Update `project(ColdFlame VERSION ...)` in CMakeLists.txt and the relevant documentation.
2. Merge or push the tested release source to main. The release source must include the packaging workflow and scripts.
3. Create a tag such as `v0.3.2` on that commit, then publish a GitHub release for the tag. Tags used in filenames may contain letters, digits, dots, underscores and hyphens.
4. The **Release applications** workflow builds the exact tagged source. It attaches the tested applications and checksums to that existing release only after all four platform builds succeed. Published prereleases use the same workflow. Creating a draft does not trigger it until publication.

The workflow does not create a release automatically when main changes. CI snapshots are retained as workflow artifacts for 14 days. Release attachments remain on the release.

To retry attachments for an existing release, use **Actions → Release applications → Run workflow** and enter its tag. Existing assets with the same filenames are replaced. A failed platform prevents the publish job, so a failed build does not upload a partial new set of downloads. A rerun does not remove older unrelated release assets.

## Local packaging

On the target operating system, install Qt 6.8+ and the normal build dependencies. Windows additionally needs NSIS and static zlib (the CI uses the vcpkg `x64-windows-static-md` triplet).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCOLD_FLAME_DEPLOY=ON
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
python scripts/package.py --platform linux --arch x86_64 --version v0.3.2
```

Select `macos` with `arm64` or `x86_64`, or `windows` with `x86_64`, as appropriate. macOS cross-architecture builds need an explicit `CMAKE_OSX_ARCHITECTURES`; the two CI packages are compiled and tested on their corresponding hardware rather than cross-tested. The package script rejects platform mismatches, checks the payload includes Qt Core and a native platform plugin, and launches the installed executable with the build machine's Qt environment removed.

Runtime deployment uses [Qt's native deployment API](https://doc.qt.io/qt-6.8/qt-deploy-runtime-dependencies.html). Runner architectures follow the [GitHub-hosted runner reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).
