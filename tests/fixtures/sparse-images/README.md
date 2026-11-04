# Artificial native sparse image references

These fixtures contain generated test files only. `provenance.json` records the
macOS creator, public test password, original member hashes and byte ranges read
independently from read-only attached devices before detaching them. The fixture
members are stored with `qCompress`-compatible zlib framing to keep empty sparse
areas compact; tests restore the original native bytes into disposable paths.

For each ordinary/AES-128/AES-256 variant, create an artificial `files` directory
containing `hello 空.txt` (`Sparse image fixture` plus newline) and `pattern.bin`.
The latter has 1,049,307 bytes with byte n equal to `(n*71+n/1024) % 256`, using
integer division. Create each image with `hdiutil create -srcfolder files -fs HFS+
-size 32m -format UDSP` or `-format UDSB`. For encrypted variants add
`-encryption AES-128` or `AES-256` and `-stdinpass`; pass the NUL-terminated public
password from provenance. Salts and filesystem metadata can differ on regeneration.

Attach each resulting image with `hdiutil attach -readonly -nomount -plist`
(and `-stdinpass` for encryption), obtain the device size with `diskutil info
-plist`, and read the recorded ranges from its raw device with aligned sector
reads. Detach only that fixture's device. No native utility or attached device
is required when these fixtures run on Linux/Windows.

The constructed sparseimage/index and sparsebundle tests supplement these native
references with holes, older 3DES wrapping, random seeks and damage. They do not
establish compatibility with an untested historical native producer.
