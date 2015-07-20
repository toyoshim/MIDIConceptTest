#include <Windows.h>
#include <mmreg.h>

#include <atlbase.h>
#include <atlconv.h>
#include <stdio.h>
#include <tchar.h>

#include <string>

// Equivalent to the Chrome check code.
bool IsUnsupportedDevice(MIDIOUTCAPS2& caps) {
	return caps.wMid == MM_MICROSOFT && (caps.wPid == MM_MSFT_WDMAUDIO_MIDIOUT || caps.wPid == MM_MSFT_GENERIC_MIDISYNTH);
}

// Test code for http://crbug.com/508587
int main() {
	USES_CONVERSION;
	puts("=== MIDI Output device information ===");

	UINT uNumDevs = midiOutGetNumDevs();
	printf("%d device(s) found:\n");
	for (UINT i = 0; i < uNumDevs; ++i) {
		MIDIOUTCAPS2 caps;
		midiOutGetDevCaps(i, reinterpret_cast<LPMIDIOUTCAPS>(&caps), sizeof(caps));
		std::string name = T2A(caps.szPname);
		printf("  %d: %s\n", i + 1, name.c_str());
		printf("    MID: %08x\n", caps.wMid);
		printf("    PID: %08x\n", caps.wPid);
		if (IsUnsupportedDevice(caps))
			puts("      This device is disabled in Chrome.");
		if (caps.wTechnology == MOD_SWSYNTH)
			puts("      This device is declared as a software synth.");
	}
	fflush(stdout);
	fgetc(stdin);
	return 0;
}
