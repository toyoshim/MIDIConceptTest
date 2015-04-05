#define _CRT_SECURE_NO_WARNINGS 1

#include <Windows.h>
#include <Dbt.h>

#include <atlbase.h>
#include <atlconv.h>
#include <stdarg.h>
#include <stdio.h>
#include <tchar.h>

#include <list>
#include <map>
#include <set>
#include <string>
#include <sstream>
#include <vector>


int DPrintf(const char* fmt, ...) {
	char buffer[4096];
	va_list args;
	va_start(args, fmt);
	int result = vsnprintf(buffer, 4096, fmt, args);
	OutputDebugStringA(buffer);
	va_end(args);
	return result;
}

struct MidiProduct {
	MidiProduct(const MIDIINCAPS2& caps) {
		USES_CONVERSION;
		mPid = caps.wPid;
		mMid = caps.wMid;
		mVersion = caps.vDriverVersion;
		mName = T2A(caps.szPname);
	};

	virtual std::string getDisplayName() const {
		return mName;
	};

	std::string toString() const {
		std::stringstream ss;
		ss << mPid << "," << mMid << "," << getDisplayName() << "," << mVersion;
		return ss.str();
	};

	bool operator==(const MidiProduct& device) const {
		return mName.compare(device.mName) == 0 && mPid == device.mPid && mMid == device.mMid && mVersion == device.mVersion;
	}

	std::string mName;
	int mPid;
	int mMid;
	int mVersion;
};

struct MidiDevice : MidiProduct {
	MidiDevice(const MIDIINCAPS2& caps, int index) : MidiProduct(caps), mIndex(index) {}
	MidiDevice(const MidiProduct& product, int index) : MidiProduct(product), mIndex(index) {}

	std::string getDisplayName() const override {
		std::stringstream ss;
		ss << MidiProduct::getDisplayName() << " (Port " << mIndex << ")";
		return ss.str();
	};

	bool operator==(const MidiDevice& device) const {
		return *static_cast<const MidiProduct*>(this) == static_cast<const MidiProduct&>(device) && mIndex == device.mIndex;
	}

	int mIndex;
};

bool operator<(const MidiProduct& lhs, const MidiProduct& rhs) {
	int result = lhs.mName.compare(rhs.mName);
	if (0 != result)
		return result < 0;
	result = lhs.mPid - rhs.mPid;
	if (0 != result)
		return result < 0;
	result = lhs.mMid - rhs.mMid;
	if (0 != result)
		return result < 0;
	return lhs.mVersion < rhs.mVersion;
};

bool operator<(const MidiDevice& lhs, const MidiDevice& rhs) {
	if (lhs.mIndex == rhs.mIndex)
		return static_cast<const MidiProduct&>(lhs) < static_cast<const MidiProduct&>(rhs);
	return lhs.mIndex < rhs.mIndex;
};

std::map<MidiProduct, std::set<int>> deviceMap;
std::list<MidiDevice> deviceList;

int NextDeviceIndex(const MidiProduct& device) {
	auto map = deviceMap.find(device);
	if (map == deviceMap.end())
		return 0;
	for (int id = 0;; id++) {
		bool found = false;
		for (auto n : map->second) {
			if (id == n) {
				found = true;
				break;
			}
		}
		if (found)
			continue;
		return id;
	}
	return -1;
}

void AddDevice(const MidiDevice& device) {
	DPrintf("Device Add: %s\n", device.toString().c_str());
	auto map = deviceMap.find(device);
	if (map == deviceMap.end()) {
		deviceMap.insert(std::make_pair(device, std::set<int>()));
		map = deviceMap.find(device);
	}
	map->second.insert(device.mIndex);
	deviceList.push_back(device);
}

void RemoveProduct(const MidiProduct& product) {
	DPrintf("Device Remove: %s\n", product.toString().c_str());
	auto map = deviceMap.find(product);
	if (map == deviceMap.end()) {
		DPrintf("**** should not happen: device not found in map ****");
		return;
	}
	auto i = map->second.rbegin();
	int index = *i;
	map->second.erase(*i);
	deviceList.remove(MidiDevice(map->first, index));
}

void UpdateDeviceList() {
	DPrintf("UpdateDeviceList\n");

	// Add new devices.
	std::map<MidiProduct, int> newDeviceMap;
	UINT uNumDevs = midiInGetNumDevs();
	for (UINT i = 0; i < uNumDevs; ++i) {
		MIDIINCAPS2 caps;
		midiInGetDevCaps(i, reinterpret_cast<LPMIDIINCAPS>(&caps), sizeof(caps));
		MidiProduct device(caps);
		auto newEntry = newDeviceMap.find(device);
		int newDeviceCount = 1;
		if (newEntry == newDeviceMap.end()) {
			newDeviceMap.insert(std::make_pair(device, 1));
		} else {
			newEntry->second++;
			newDeviceCount = newEntry->second;
		}
		auto entry = deviceMap.find(device);
		int deviceCount = 0;
		if (entry != deviceMap.end())
			deviceCount = entry->second.size();
		if (newDeviceCount > deviceCount)
			AddDevice(MidiDevice(caps, NextDeviceIndex(device)));
	}

	// Remove disconnected devices that are not opened.
	// If we have multiple sam edevices, we can not figure out which device is disconnected.
	std::vector<MidiProduct> removeList;
	for (auto device : deviceMap) {
		int disconnectedDevices = device.second.size();
		auto newMap = newDeviceMap.find(device.first);
		if (newMap != newDeviceMap.end())
			disconnectedDevices -= newMap->second;
		for (int i = 0; i < disconnectedDevices; ++i)
			removeList.push_back(device.first);
	}
	// So, let's remove the last device.
	for (auto device : removeList)
		RemoveProduct(device);

	DPrintf("=== new device list ===\n");
	for (auto device : deviceList)
		DPrintf("%s\n", device.toString().c_str());
	DPrintf("=== new device map ===\n");
	for (auto product : deviceMap)
		DPrintf("%s: x%d\n", product.first.getDisplayName().c_str(), product.second.size());
}

LRESULT CALLBACK DeviceNotifyWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_DEVICECHANGE:
		if (wParam == DBT_DEVNODES_CHANGED) {
			DPrintf("WM_DEVICECHANGE: DBT_DEVNODES_CHANGES\n");
			UpdateDeviceList();
		} else {
			// DBT_DEVICEARRIVAL and DBT_DEVICEREMOVECOMPLETE will be notified if it is enabled by RegisterDeviceNotification.
			DPrintf("WM_DEVICECHANGE: wParam=%04x, lParam=%04x\n", wParam, lParam);
		}
		return TRUE;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

int APIENTRY _tWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPTSTR lpCmdLine, _In_ int nCmdShow) {
	// Use 
	WNDCLASSEX wcex;
	ZeroMemory(&wcex, sizeof(wcex));
	wcex.cbSize = sizeof(wcex);
	wcex.lpfnWndProc = DeviceNotifyWindowProc;
	wcex.hInstance = hInstance;
	wcex.lpszClassName = _T("DeviceNotifyWindowClass");
	RegisterClassEx(&wcex);
	HWND hWnd = CreateWindow(wcex.lpszClassName, _T("MIDIConectpTest - DeviceNotify"), 0, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);

	UpdateDeviceList();

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
		DispatchMessage(&msg);
	return 0;
}
