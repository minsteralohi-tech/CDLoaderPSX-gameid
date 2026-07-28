# MemCardPro-ASM

The Game-ID transport in `src/memcardpro.S` and
`src/memcardpro_regs.inc` is based on the official
[Cybdyn Systems MemCardPro-ASM library](https://github.com/Cybdyn-Systems/MemCardPro-ASM).

- Upstream source: `ELF/MCRDPRO.ASM` and `ELF/REGS.ASM`
- Copyright: 2021-2024 Cybdyn Systems
- License: Apache License 2.0 (see `LICENSE` in this directory)

Project-local changes are limited to normalizing whitespace for the repository,
making two comments safe for the C preprocessor, and adjusting the include path
used for the register-definition file.
