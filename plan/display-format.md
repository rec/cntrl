# Display formatting

The current TOML formatting contract is documented in
[formatting.md](formatting.md). It replaces this earlier design, including
category priorities and authored font sizing. Compiled document format 14 uses
independently fitted lines and preserves partial styles, category assignments
and optional icon references. These changes have not been deployed to the pedal.

Pages retain ordered unique field names, with the default page displaying
`group.name` and `display_name`. Windows supply their own data item to each page.
Missing or empty values are omitted; zero and false remain visible. Shared
parameters occupy the same field namespace as built-ins. Unknown or duplicate
field references are errors. Pages are bounded to 16 fields and the target's
available height. See the [firmware documentation](../firmware/README.md) for
field names and the compiled protocol.
