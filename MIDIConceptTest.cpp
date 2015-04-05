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
	MidiDevice(const MIDIINCAPS2& caps, int index) : MidiProduct(caps), mIndex(index), mHMidi(NULL) {}
	MidiDevice(const MidiProduct& product, int index) : MidiProduct(product), mIndex(index), mHMidi(NULL) {}

	std::string getDisplayName() const override {
		std::stringstream ss;
		ss << MidiProduct::getDisplayName() << " (Port " << mIndex << ")";
		return ss.str();
	};

	bool operator==(const MidiDevice& device) const {
		return *static_cast<const MidiProduct*>(this) == static_cast<const MidiProduct&>(device) && mIndex == device.mIndex;
	}

	int mIndex;
	HMIDIIN mHMidi;
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

std::map<MidiProduct, std::set<int>> productMap;
std::map<HMIDIIN, MidiDevice> handleMap;
std::set<HMIDIIN> closingHandleSet;
std::set<MidiDevice> deviceSet;

int NextDeviceIndex(const MidiProduct& device) {
	auto map = productMap.find(device);
	if (map == productMap.end())
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
	DPrintf("Device Add: %s (HMIDIIN: %08x)\n", device.toString().c_str(), device.mHMidi);
	auto map = productMap.find(device);
	if (map == productMap.end()) {
		productMap.insert(std::make_pair(device, std::set<int>()));
		map = productMap.find(device);
	}
	map->second.insert(device.mIndex);
	deviceSet.insert(device);
}

void RemoveDevice(const MidiDevice& device) {
	DPrintf("Device Remove: %s\n", device.toString().c_str());
	productMap.find(device)->second.erase(device.mIndex);
	deviceSet.erase(device);
}

void RemoveProduct(const MidiProduct& product) {
	DPrintf("Product Remove: %s\n", product.toString().c_str());
	auto map = productMap.find(product);
	auto index = map->second.rbegin();
	if (index == map->second.rend())
		return;
	RemoveDevice(MidiDevice(product, *index));
}

void CALLBACK MidiInCallback(HMIDIIN hMidiIn, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2) {
	if (uMsg != MIM_CLOSE)
		return;
	auto device = handleMap.find(hMidiIn);
	if (device == handleMap.end())
		return;
	RemoveDevice(device->second);
	handleMap.erase(hMidiIn);
	// midiInClose should be called later (Note: forbidden to call from here)
	closingHandleSet.insert(hMidiIn);
}

void UpdateDeviceList() {
	DPrintf("UpdateDeviceList\n");

	// Add new devices.
	std::map<MidiProduct, int> newProductMap;
	UINT uNumDevs = midiInGetNumDevs();
	for (UINT i = 0; i < uNumDevs; ++i) {
		MIDIINCAPS2 caps;
		midiInGetDevCaps(i, reinterpret_cast<LPMIDIINCAPS>(&caps), sizeof(caps));
		MidiProduct product(caps);
		auto newEntry = newProductMap.find(product);
		int newDeviceCount = 1;
		if (newEntry == newProductMap.end()) {
			newProductMap.insert(std::make_pair(product, 1));
		} else {
			newEntry->second++;
			newDeviceCount = newEntry->second;
		}
		auto entry = productMap.find(product);
		int deviceCount = 0;
		if (entry != productMap.end())
			deviceCount = entry->second.size();
		if (newDeviceCount > deviceCount) {
			MidiDevice device(caps, NextDeviceIndex(product));
#ifndef NOPEN  // If you do not want to open the device here, define NOPEN.
			midiInOpen(&device.mHMidi, i, reinterpret_cast<DWORD_PTR>(&MidiInCallback), NULL, CALLBACK_FUNCTION);
			handleMap.insert(std::make_pair(device.mHMidi, device));
#endif
			AddDevice(device);
		}
	}

	// Remove disconnected devices that are not opened.
	// If we have multiple sam edevices, we can not figure out which device is disconnected.
	std::vector<MidiProduct> removeList;
	for (auto device : productMap) {
		int disconnectedDevices = device.second.size();
		auto newMap = newProductMap.find(device.first);
		if (newMap != newProductMap.end())
			disconnectedDevices -= newMap->second;
		for (int i = 0; i < disconnectedDevices; ++i)
			removeList.push_back(device.first);
	}
	// So, let's remove the last device.
	for (auto device : removeList)
		RemoveProduct(device);

	for (auto handle : closingHandleSet)
		midiInClose(handle);
	closingHandleSet.clear();

	DPrintf("=== new device list ===\n");
	for (auto device : deviceSet)
		DPrintf("%s\n", device.toString().c_str());
	DPrintf("=== new device map ===\n");
	for (auto product : productMap)
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

// Experimental code for http://crbug.com/472980
int APIENTRY _tWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPTSTR lpCmdLine, _In_ int nCmdShow) {
	WNDCLASSEX wcex;
	ZeroMemory(&wcex, sizeof(wcex));
	wcex.cbSize = sizeof(wcex);
	wcex.lpfnWndProc = DeviceNotifyWindowProc;
	wcex.hInstance = hInstance;
	wcex.lpszClassName = _T("DeviceNotifyWindowClass");
	RegisterClassEx(&wcex);
	HWND hWnd = CreateWindow(wcex.lpszClassName, _T("MIDIConectpTest - DeviceNotify"), 0, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);

#if 0
	DEV_BROADCAST_DEVICEINTERFACE filter;
	filter.dbcc_size = sizeof(filter);
	filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	RegisterDeviceNotification(hWnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE | DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);
#endif

	UpdateDeviceList();

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
		DispatchMessage(&msg);
	return 0;
}
