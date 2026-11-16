#pragma once
// Explicit override: -DEII_DIAGNOSTICS=0/1. Release excludes instrumentation.
#ifndef EII_DIAGNOSTICS
#ifdef NDEBUG
#define EII_DIAGNOSTICS 0
#else
#define EII_DIAGNOSTICS 1
#endif
#endif
#if EII_DIAGNOSTICS != 0 && EII_DIAGNOSTICS != 1
#error EII_DIAGNOSTICS must be 0 or 1
#endif
namespace eii { inline constexpr bool diagnosticsEnabled = EII_DIAGNOSTICS != 0; }
