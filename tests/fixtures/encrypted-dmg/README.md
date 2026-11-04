# Artificial native DMG references

Created on macOS with `hdiutil`, 2026-11-03. These images contain only generated
test data. `provenance.json` records their hashes and public test password.
No mounted image or native tool is needed when running tests on another OS.

`native-raw128.dmg` and `native-raw256.dmg` are encrypted raw images containing
65536 zero bytes. Their v2 password slots use native AES-192 wrapping.
`native-hfs256.dmg` is encrypted compressed UDIF containing an HFS+ filesystem;
`native-hfs-clear.dmg` is an ordinary compressed UDIF of the same artificial
folder. The only source file is `hello 空.txt`, containing
`Encrypted DMG fixture` followed by a newline.

To regenerate in a disposable directory (native metadata and salts may differ):

```sh
mkdir files
printf 'Encrypted DMG fixture\n' > 'files/hello 空.txt'
printf 'fixture-password\0' | hdiutil create -size 64k -layout NONE \
  -encryption AES-128 -stdinpass native-raw128.dmg
printf 'fixture-password\0' | hdiutil create -size 64k -layout NONE \
  -encryption AES-256 -stdinpass native-raw256.dmg
printf 'fixture-password\0' | hdiutil create -srcfolder files -fs HFS+ \
  -format UDZO -encryption AES-256 -stdinpass native-hfs256.dmg
hdiutil create -srcfolder files -fs HFS+ -format UDZO native-hfs-clear.dmg
```

The tests independently construct patterned v1/v2 streams with fixed keys for
older 3DES wrapping, partial chunks and malformed-input scenarios. Those fixtures
are artificial format regressions, not proof of compatibility with a historical
native v1 producer. Native raw images supply an independent complete plaintext
reference for random reads, boundaries and EOF.

`native-apfs-clear.dmg` is a native compressed UDIF/APFS image containing generated
data. Its `inline.txt` has a synthetic `com.apple.decmpfs` method-3 attribute and
the compressed BSD flag. Before imaging, the macOS kernel read the complete
32768-byte repeating 0..255 payload successfully; provenance records its hash.
The image was produced with `hdiutil create -srcfolder ... -fs APFS -format UDZO`.
Tests wrap it in deterministic encryption and read it through the mounted-access
boundary with no extraction cache. Resource-fork compression is covered separately
by synthetic HFS fixtures; this image does not establish native resource-fork or
historical v1 compatibility.
