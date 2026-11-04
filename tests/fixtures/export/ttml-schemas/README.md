# TTML and IMSC1 schemas

`hikari_core_export_tests` checks the TTML and DFXP goldens against these schemas with xmllint (`check_ttml_schema.cmake`). `validate.xsd` is ours and only imports the W3C files.

- `ttml1/`: the TTML1 (Second Edition) schemas, unchanged, from <http://www.w3.org/2013/09/ttml1/xsd/schema.zip>.
- `imsc1-*.xsd`: the IMSC1 extension schemas, unchanged, from <https://www.w3.org/TR/ttml-imsc/xml-schemas/>.

The W3C files are © World Wide Web Consortium and are distributed under the W3C Software and Document License, in `LICENSE`.
