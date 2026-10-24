# Installer candidate review — 2026-10-23

The 0.1.2 candidate is under review in [draft PR #1](https://github.com/DinoRex4848/OBS-Multi-Replay-Buffer/pull/1)
on `installer/offline-setup-and-linux-guidance`, based on `941ac7f`. It has not been
merged, tagged or released. Windows delivery is an extracted ZIP with **install.bat**
and **uninstall.bat**; Linux delivery is a Flatpak `.run` plus an alternative tar.
No new graphical Windows setup or setup compiler is required.

## Findings and supported scope

The historical 0.1.0 setup used a global mutex and waited for a helper that launched
an elevated copy of itself and waited. This is a plausible explanation for an
interactive setup stall, verified in source but not reproduced on native Windows.
The friend's exact error and OBS version remain unknown. Current batch launchers
use a shared transaction engine: inspection releases its lock before elevation,
and the parent holds no installer mutex while waiting for the elevated child.

The existing engine already provides hash/architecture checks, running-OBS and
concurrent-operation guards, staged replacement, rollback/recovery, version checks,
and legacy EXE migration without running the obsolete uninstaller. These remain.
The only `src/` change replaces the module's single FFmpeg call (see below).

Windows supports normal and explicit portable OBS on Windows 10/11 x64, built with
the OBS 32.1.1 SDK. The OBS 32.1 minimum is not a blanket future-ABI guarantee.
The normal destination is ProgramData; portable mode uses the selected OBS tree.
Settings, scenes, hotkeys, recordings and unrelated plugins remain untouched.

Linux's friend-facing package is specifically x86_64 Flatpak OBS 32.2.2, exact
commit `21b35765922fb5c8b92ccdccedc0d8ca709b12f86a961780350199082da822ef`, using its
FFmpeg 8 ABI. The exact-commit and ABI guards are unchanged. Fedora 44's native
archive remains developer tooling, not an interchangeable Flatpak or deb installer.

## Final changes

- Rename the Windows launchers to `install.bat` / `uninstall.bat`, using built-in
  PowerShell and process-local execution policy. Normal OBS is the default choice;
  elevation is requested only when needed, with visible errors and persistent logs.
- Detect custom normal OBS paths using the application key's default value,
  uninstall InstallLocation values, both registry views and Program Files.
  Normal mode refuses portable markers with instructions to choose Portable.
- Keep safe update/repair/removal and old-EXE cleanup. No new Installed Apps entry;
  use `uninstall.bat`, including after OBS or the payload has been removed.
- Add one offline Linux `.run`, with an embedded archive hash, safe extraction,
  working confirmation input, temporary files and persistent user-state logs.
  It uses exactly the same payload and transaction engine as the tar archive.
- Extend preservation fixtures for scenes, hotkeys and recordings. Windows native
  acceptance exercises the batch default-normal confirmation/discovery path,
  portable relocation, payload damage, concurrency, repair/removal and dependencies.
- Enable actual Windows OBS startup/shutdown checks and preserve special-character
  test paths using ProcessStartInfo rather than PowerShell wildcard resolution.
  Linux production acceptance now exercises `.run` and tar launchers.
- Draft-release gates require the Windows ZIP, Linux run/tar, checksums, notices
  and Linux compatibility metadata. The `.run` must embed the exact validated tar.
  Published releases remain protected and branch CI cannot publish a release.

## Evidence and remaining checks

Local GCC history/naming tests pass. Linux fixture suites pass (14 transaction,
14 package, 9 release-gate tests). Windows filesystem/manifests/rollback fixtures
pass under PowerShell 7.4.13 on Linux; that is not native Windows acceptance.
Python compilation, shell syntax and whitespace checks pass. Fixture binaries are
non-executable sentinels and must never be distributed as plugins.

[CI run 40039408059](https://github.com/DinoRex4848/OBS-Multi-Replay-Buffer/actions/runs/40039408059)
on `22d7391` passed Fedora native compilation/core tests and Flatpak production
builds, real compatibility checks, install/repair/removal for both Linux formats,
and isolated OBS load/shutdown. Baseline and plugin runs both retained one OBS
allocation, with normal configuration hashes unchanged. Windows MSVC and setup
compilation passed on that earlier revision; acceptance exposed wildcard handling
in the previously unexecuted smoke launcher. The literal-path fix is retained.
[Run 40039877702](https://github.com/DinoRex4848/OBS-Multi-Replay-Buffer/actions/runs/40039877702)
on `a278557` reached and completed the standard script install/repair/removal cases
after the DLL/writer and isolated OBS startup/shutdown assertions. Its later failure
was a timeout in an EXE-only test. The EXE builder and all wizard-only obligations
are removed from the final batch-only scope; actual OBS loading checks remain.

A non-admin native run on Windows 11 found two blockers. Microsoft Defender
flagged the elevated `-EncodedCommand` relaunch as `!#SLF:CMD_HSTR:General.ML.B` and
blocked it ("Access is denied" before UAC); the relaunch now uses `-File` with quoted
arguments, and UAC install/repair/uninstall then passed from Desktop and Downloads.
Steam OBS 32.2.2 could not load the module because it imported FFmpeg 7's
`avutil-59.dll` while OBS 32.2 ships FFmpeg 8; the module no longer imports FFmpeg,
and packaging fails if it ever does. A rebuilt local package then installed, loaded
and started a replay session in Steam OBS 32.2.2. The elevated result is shown in the original
window, and OneDrive cloud reparse points are accepted while links stay refused.

Native Windows batch-package acceptance and the OBS smoke must pass on the final
revision before distribution. Hosted builders are already administrators, so UAC
approval/denial and protected portable folders remain manual checks. Real hardware
replay saves, audio/video quality and desktop hotkeys also remain manual acceptance.
A startup/load smoke does not establish successful real replay capture.

The cloud host's proxy rejects dependency/download domains with HTTP 403 CONNECT;
CI establishes production results independently. Those network failures were not
approval-review denials. Local test logs and synthetic packages are under ignored
`build-installer-review/`; downloaded fixture artifacts are never release inputs.

## Publication plan

Keep this PR a draft until the final native Windows and Linux checks pass. Download
only production CI artifacts for clean-desktop BAT/UAC and real replay acceptance.
Review the exact OBS compatibility metadata and dependency notices. After explicit
approval, merge and tag `v0.1.2` at the accepted commit. Tag CI prepares only an
unpublished draft prerelease; inspect all three assets and checksums before making
a separate decision to publish. No merge, tag or release is authorized here.
