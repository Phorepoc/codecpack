// Copyright The Phorepoc Authors
// SPDX-License-Identifier: EPL-2.0 OR GPL-2.0-or-later

// fft_sys.cpp -- OURS: the Symbian side of phefftest, callable from C: debug output ("PH: ..." lines go to the kernel
// seam through RDebug) and a microsecond clock.
#include <e32std.h>
#include <e32debug.h>

extern "C" void ph_puts(const char* aS)
	{
	RDebug::Printf("%s", aS);
	}

extern "C" long long ph_us(void)
	{
	TTime t;
	t.UniversalTime();
	return t.Int64();
	}
