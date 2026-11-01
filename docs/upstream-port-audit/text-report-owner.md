# Shared text-report presentation — 2026-11-01

Reference: <https://github.com/ip7z/7zip.git>, commit
`9128b80e3a471108678e2b3ec3c985861c8d7b0f`. Compared EditDialog bodies,
header/resource styles and ListViewDialog's full-value consumer, plus Benchmark
normal/total resources, Qt report consumers and lifecycle/copy regressions.
Complete original EditDialog.cpp/.h/.rc and BenchmarkDialog.cpp/.rc hashes match
the existing baseline. No external source import or revision advancement.

Full-value details, the Benchmark console and its log independently configured
the same read-only, unwrapped, monospaced text view. Full-value details and the
console also duplicated selected-or-full-text copying. These local Qt adaptations
now share a stateless report module and target, sevenzip_text_report. Consumers
retain their QPlainTextEdit widgets, object names and explicitly selected base
font, preserving text size and native font resolution. No additional widget
class, copied model, flags or lifetime owner is introduced.

Source hashing, digest aggregation, result rows and full-value storage are
unchanged. Benchmark's worker, timers, restart/cancel/join, tab order and normal
table export remain with their existing owners. The detail's Enter filter and
initial Close focus remain there too. The total console retains its empty-text
guard and button availability. Copy preserves selection and does not close
either dialog. No resource, language, text, geometry or key-binding redesign.

The new module is recorded as local integration in the existing inventory;
original-byte hashes remain unchanged. Existing tests already check raw Unicode
text, selected/full copying, read-only input, Enter/Tab/Close, horizontal/vertical
scrolling, normal/compact geometry, Benchmark lifecycle and direct hash consumers.
No source-shape test or duplicate widget-specific test was added.

Validation: edit_dialog, list_view_dialog, hash_results, namespace_hash,
native_hash_ui, benchmark_lifecycle and benchmark_command passed first on Linux,
then macOS and Windows. Linux/macOS reported 53 passes each; Windows reported
57 passes and two expected POSIX-affinity fixture skips. No failures. Current
normal/compact widget JSON was reviewed on all three platforms: 18 reports each,
with visible text/content areas and Copy/footer controls, fitting buttons and no
content/footer overlap. This is widget-contract evidence, not original Windows
pixel parity. Selected original-unit hashes and the updated local inventory were
verified. Incremental outputs/caches retained; completed fixtures and disposable
JSON removed after evidence capture. No full/slow gate or heavy diagnostics.
