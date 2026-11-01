# Runtime UI comparison for agents

`scripts/compare_ui_runtime.py` compares actual original 7zFM captures with
native Qt captures on Windows, Linux and macOS. It emits JSON on stdout,
optionally also to `--output`. It does not launch applications or perform
archive operations. This is local inspection tooling, not a port of an upstream
functional module: upstream does not compare its Win32 windows with this Qt port.

Exit codes: **0** all declared cases/platforms pass; **1** differences, missing
captures/platforms or unusable capture evidence; **2** invalid matrix/contract.
Treat `passed` as acceptance of the explicit checks only. Read `missing`, each
result's `errors`, `measurements`, `adaptations` and top-level `limitations`.
Do not equate a geometry PASS with visual or behavioral parity.

## Copy/Move workflow

1. Review the original resource, handlers and callers as required by
   [the parity workflow](../.agents/skills/sz-ui-parity/SKILL.md). The Copy contract
   maps direct dialog children using IDs from `CopyDialogRes.h` and standard
   IDOK/IDCANCEL. The shared pinned reference records source hashes/commit.
   Runtime EXE identity is checked separately; its hash does not prove that it
   was built from the source commit. Record that correspondence in the audit.
2. Obtain the SHA-256 of the reviewed original `7zFM.exe`. Initialize a matrix
   in the evidence directory (the command refuses to overwrite an existing file):

   ```sh
   python3 scripts/compare_ui_runtime.py \
     --init-copy-matrix build/ui-runtime/matrix.json \
     --reference-exe-sha256 <64-lowercase-hex-digits>
   ```

   Create the parent directory first. The result describes eight cases:
   Copy/Move × EN/RU × initial/compact, each requiring all three platforms.
   Initialization reports `capture-required`; it supplies no acceptance.
3. Collect the original with
   [capture-7zfm-reference.ps1](windows-ui-reference.md). Use actual application
   windows, not `-TemplatesFrom`. Set `-Language en`/`ru` and `-State` to
   `copy-initial`, `copy-compact`, `move-initial` or `move-compact` as appropriate.
   Select the corresponding actual language/action in 7zFM; capture labels do
   not change the application. Use disposable identical selection fixtures.
   Set compact client size deliberately, and retain DPI/size evidence.
   Update matrix `reference` paths to the resulting JSON files.
4. Build `copy_dialog_tests` on each platform after synchronizing the current
   source. Run its `sourceFrameAndKeyboard` case with the native QPA backend,
   `SEVENZIP_UI_REPORTS=ON`, `SEVENZIP_UI_REPORT_DIR` pointing to the platform evidence directory and,
   when appearance evidence is needed, `SEVENZIP_CAPTURE_SCREENSHOTS=1`.
   See [native GUI commands](development.md) and [runners](hyperv-runners.md).
   The ordinary CTest producer uses offscreen; those reports are useful for
   resource checks but deliberately rejected for runtime comparison.
   Keep the generated `copy-source-frame-*-9-{initial,compact}.json` files
   under the matrix's `windows/`, `linux/`, `macos/` directories, or adjust paths.
   These producers record actual QPA/platform and scenario language/state.
   The 12pt/32pt cases remain useful separate scaling probes.
   For example, the local macOS producer command after the build is:

   ```sh
   artifact_root="$(python3 scripts/lib/build_paths.py path --root "$PWD" --category artifacts)"
   QT_QPA_PLATFORM=cocoa \
   SEVENZIP_UI_REPORTS=ON SEVENZIP_UI_REPORT_DIR="$PWD/build/ui-runtime/macos" \
   SEVENZIP_SETTINGS_PATH="$PWD/build/ui-runtime/settings.ini" \
     "$artifact_root/dev/copy_dialog_tests" sourceFrameAndKeyboard -silent
   ```

   Use `windows` QPA in the Windows interactive desktop and `xcb`/`wayland`
   in the Linux graphical session; the repository runner transport alone does
   not establish that an interactive graphical session is available.
5. Run:

   ```sh
   python3 scripts/compare_ui_runtime.py \
     --matrix build/ui-runtime/matrix.json \
     --output build/ui-runtime/comparison.json
   ```

   Resolve each difference against source and the actual reference. Missing
   platforms/captures remain failures; do not remove them to obtain acceptance.
   Preserve current failure evidence and apply [artifact retention](testing.md#test-artifact-retention).

## Matrix and results

Paths in a matrix are relative to that matrix. Each case has `id`, `language`,
`state`, `reference` and `targets` (`platform`, `report`, optional `screenshot`).
Platforms are `windows`, `linux`, `macos`; `requiredPlatforms` specifies required
coverage for every declared case. Duplicate cases/platforms are invalid.
Reference screenshot paths come from the capture JSON; Qt screenshot paths,
when supplied, are relative to the Qt report. Evidence includes absolute paths
and SHA-256 hashes. Missing explicitly requested screenshots fail the case.

The comparator reuses contract selectors and spatial relations. Missing or
ambiguous mappings fail; hidden controls are mapped but excluded from spatial
checks. State, clipping and unadapted available text differences are errors.
Text/class adaptations are included in results for review; relations marked as
adaptations are still checked. Visibility and enabled-state differences cannot
be suppressed by text/class adaptations.

Bounds are compared relative to each root client area. `normalizedDelta` is
Qt minus original for `[x/clientWidth, y/clientHeight, width/clientWidth,
height/clientHeight]`. Original physical pixels and Qt logical pixels are
retained separately, together with client dimensions and capture metadata.
This measures proportions without confusing screen position or OS window frames
with layout. It does not establish equality of absolute sizes or fonts.

By default these deltas are measurements, not pass/fail thresholds. A reviewed
case may specify `maxNormalizedDelta` between 0 and 1; any visible mapped
control exceeding it fails. Do not loosen thresholds or add adaptations merely
to hide unexpected differences. Pixel rendering, icons, colors, focus, keyboard
order and transitions still need their respective reference/behavior checks.

The focused `ui_runtime_comparison` CTest covers CLI outcomes, scale handling,
clipping/state/geometry failures, ambiguous mappings, incomplete matrices,
template/offscreen rejection and mismatched EXE/scenario evidence. Synthetic
fixtures verify the tool, not the application's three-platform UI acceptance.
