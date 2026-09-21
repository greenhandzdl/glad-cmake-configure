// ============================================================================
// gldxcli - module implementation unit.
//
// Flags is fully inline in the primary interface (gldxcli.cppm), so this unit
// exists only to give the `gldxcli` static library a concrete translation
// unit to compile/archive alongside the module interface file set. Keeping the
// parser header-style (all-inline) mirrors its origin as demo_cli.h and avoids
// splitting a small utility across two units for no benefit.
// ============================================================================

module gldxcli;
