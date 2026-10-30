# Open Fixture Library schema snapshot

Unmodified OFL schema 12.6.0, taken from commit
`312ff119135c0a33874a863361906d6fe9472f59` of
https://github.com/OpenLightingProject/open-fixture-library.
MIT license reproduced in LICENSE. The desk-channel regression fixture is from
the same upstream repository and is covered by that license.

Used only in offline tests, with `jsonschema==4.26.0` (Python), to validate the
actual JSON written by the C++ exporter. The application has no Python or schema
download dependency. Runtime import performs structural and output-safety
checks, not full JSON Schema validation. OFL's own downloaded files add three
export-plugin fields (`manufacturerKey`, `fixtureKey`, `oflURL`) that are not
part of this repository schema; the importer accepts and preserves them.
