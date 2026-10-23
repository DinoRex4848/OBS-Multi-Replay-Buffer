# Installer candidate review — 2026-10-23

This is a local **0.1.2 candidate**, based on `941ac7f`, on branch
`installer/offline-setup-and-linux-guidance`. Nothing has been pushed, merged,
tagged or published. There are no new production installer binaries ready to send
friends. Fixture artifacts contain deliberately non-executable plugin payloads.

## Findings

The historical 0.1.0 Inno source uses `PrivilegesRequired=lowest`, a global
`SetupMutex`, and a `NextButtonClick` path that waits for `Setup.Relaunch.ps1`.
That helper starts the same EXE with RunAs and waits for it. A mutex-owning parent
waiting on the elevated copy is a plausible explanation for an interactive setup
stall. This was verified in source, **not reproduced on native Windows**. The old
silent CI path supplied `/ALLUSERS`, bypassing that interactive elevation path.
The friend's exact error and OBS version remain unknown.

The 0.1.1 checkout ships CMD/PowerShell and shell/Python archives, with useful
transaction tests but no conventional Windows setup. Its OBS discovery checks
uninstall InstallLocation values and Program Files; the new shared discovery also
checks the OBS application key's default value and both registry views.

Linux is not one interchangeable binary target: the Flatpak package pins OBS
32.2.2's exact deployment commit and FFmpeg 8 ABI. These guards remain intact.
Fedora 44's native archive remains explicitly developer tooling. There is no
new deb/RPM installer or claim of universal Linux compatibility.

## Changes

- `packaging/windows/setup.iss`, `Setup.Backend.ps1`, and
  `scripts/package-windows-setup.ps1`: an offline setup for normal OBS, one upfront
  elevation, normal OBS folder selection, ProgramData plugin installation and
  persistent Installed Apps removal. No self-relaunch helper or privilege override.
  Portable markers direct users to the separate ZIP. The new AppId/support folder
  is separate from obsolete 0.1.0 support, which the engine can retire safely.
- `Installer.Core.ps1`: broader OBS discovery. Existing transaction, rollback,
  process, integrity and path checks are retained. The portable script path remains.
- `packaging/linux/bootstrap.py`, `scripts/package_linux.py`, and the Linux frontend:
  one offline `.run`, embedded archive hash/path checks, preserved stdin for the
  confirmation prompt, temporary extraction, and persistent user-state logs.
  Both Linux formats use the same production payload and transaction engine.
- CI: compile setup, exercise the normal graphical wizard without `/ALLUSERS` or
  silent mode, test Installed Apps registration/repair/removal, and enable the
  previously optional isolated Windows OBS startup/shutdown smoke. Production
  Linux acceptance now exercises `.run` and tar launchers. These CI changes have
  **not run on this revision**.
- Release gates require setup EXE, portable ZIP, Linux run/tar, checksums and
  notices. The `.run` must embed the exact validated tar. Draft-only publication
  behavior and refusal to modify published releases remain.
- Replay implementation under `src/` is unchanged. CMake version becomes 0.1.2;
  profile/scene/hotkey/recording sentinels extend preservation coverage.

## Checks in this Linux environment

| Check | Result and scope |
| --- | --- |
| History and naming | Both compiled directly with GCC 14 and passed. Replay core only; not a plugin/OBS build. |
| `tests/flatpak_installer_tests.py` | 14 passed; temporary payloads and mocked host interaction. |
| `tests/linux_package_tests.py` | 14 passed; extracted tar and real shell/bootstrap execution with synthetic ELF payloads and a fake Flatpak CLI. Includes update/repair, confirmation/cancel, corruption, unsafe paths, mismatch, sudo refusal, persistent logs, removal and preservation. |
| `tests/release_assets_tests.py` | 10 passed; synthetic assets and mocked GitHub publication. No GitHub writes. |
| `tests/windows_installer_tests.ps1` | Passed using PowerShell 7.4.13 on Linux. Parsing, startup contract, fixture ZIP/manifests, rollback/recovery, install/repair/removal, path quoting, legacy support cleanup and preservation. Not native PowerShell 5.1, registry/UAC or OBS execution. |
| Python compile, shell syntax, `git diff --check` | Passed. |
| Full Fedora/Flatpak production build and OBS smoke | Blocked before dependency resolution. Proxy returns HTTP 403 to HTTPS CONNECT for `mirrors.fedoraproject.org`, `dl.fedoraproject.org` and `dl.flathub.org`. This is network access rejection, not an approval-review denial or a compiler failure. |
| Windows setup compilation | Not completed. The Inno 6.7.3 tool was downloaded, but this host cannot execute the 32-bit Wine loader (`Exec format error`). A 64-bit Inno 7 fallback refused the Wine environment. No new EXE was produced. |
| FFmpeg dependency test | An attempted run failed before configuration because CMake was initially absent. CMake was subsequently installed in temporary tools, but matching OBS/FFmpeg 8 dependencies are unavailable; no successful dependency-guard rerun is claimed. Guards were not changed. |
| Native Windows build, normal wizard, actual registry/removal and OBS startup | Not run here; implemented as required Windows CI checks. |
| UAC approval/denial, protected portable folder, real replay saves/hotkeys | Not run; require native interactive/manual acceptance. |

Local logs and synthetic packages are under `build-installer-review/`, ignored by
Git. Read its `README.txt`: **fixture packages must never be distributed as plugins**.
No downloaded tools or dependency binaries are committed.

## Proposed next steps — require separate authorization

1. Push only this branch and open a draft PR against main. Do not tag or merge.
   The push/PR runs `.github/workflows/build.yml` on the branch. Inspect Windows
   setup compilation, ordinary wizard/repair/removal and `-SmokeObs`, plus Fedora
   and exact-commit Flatpak production jobs. Resolve any native failures before
   calling this revision tested on those platforms.
2. Download CI's production artifacts and test on a clean Windows 10/11 desktop
   with standard-user UAC approval/denial, a custom OBS location, and portable OBS;
   exercise real replay saves, saved scenes/settings/hotkeys and removal. On Linux,
   run both install/update/removal and real OBS with the exact compatible Flatpak.
   Ask friends for their original error/log and installation type when available.
3. After review and explicit permission, merge and tag `v0.1.2` at the accepted
   commit. Tag CI must pass all jobs and prepares only an unpublished draft
   prerelease. Inspect its four production assets, checksums, notices and
   compatibility metadata. Publishing that draft requires a separate decision.
