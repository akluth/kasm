# Includes, Defines, And Macros

Includes use quoted paths:

```asm
include "file.inc"
```

Defines create constants:

```asm
define CODE 42
```

Macros use:

```asm
macro name arg1, arg2
    mov rax, arg1
end
```

Macro-local labels use `%%label` and are made unique per expansion.

KASM intentionally does not implement a full NASM preprocessor. Unsupported features include conditional assembly, token pasting, recursive macro metaprogramming, `%define`, `%macro`, and include guards with conditions.

Diagnostics cover missing `end`, recursive macros, argument-count mismatches, invalid parameters, and macro names that conflict with instructions or directives.
