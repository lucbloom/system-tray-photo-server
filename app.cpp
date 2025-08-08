#define WINDOWS_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <ws2tcpip.h>
#include <mutex>
#include <random>
#pragma comment(lib, "Ws2_32.lib")

#include "Resource.h"

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 3

HWND hWnd;
NOTIFYICONDATA nid = {};
SOCKET server = INVALID_SOCKET;
std::vector<std::string> gFolders;
int gPort = 1919;
bool running = true;
std::vector<std::string> all;

std::wstring to_wstring(const std::string& str) {
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
	std::wstring wstr(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size_needed);
	wstr.resize(size_needed - 1); // remove null terminator
	return wstr;
}

std::string ini_path() {
	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, MAX_PATH);
	std::string s(path);
	return s.substr(0, s.find_last_of("\\/")) + "\\config.ini";
}

std::wstring get_appdata_folder() {
	wchar_t* appdata = nullptr;
	SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, NULL, (PWSTR*)&appdata);
	std::wstring wstr(appdata);
	CoTaskMemFree(appdata);
	auto dir = wstr + L"\\ImageServerTray";
	CreateDirectory(dir.c_str(), NULL);
	return dir;
}

void read_config() {
	char folders[1024] = {};
	GetPrivateProfileStringA("server", "folders", "photos", folders, sizeof(folders), ini_path().c_str());
	char* context = nullptr;
	char* token = strtok_s(folders, ",", &context);
	while (token) {
		std::string f(token);
		// trim whitespace
		while (!f.empty() && isspace(f.front())) f.erase(f.begin());
		while (!f.empty() && isspace(f.back())) f.pop_back();
		gFolders.push_back(f);
		token = strtok_s(NULL, ",", &context);
	}
	gFolders.push_back("D:\\Photos");
	gPort = GetPrivateProfileIntA("server", "port", 1919, ini_path().c_str());
}

std::string build_json_list(const std::vector<std::string>& list) {
	std::ostringstream oss;
	oss << "[";
	for (size_t i = 0; i < list.size(); ++i) {
		oss << "\"" << list[i] << "\"";
		if (i != list.size() - 1) oss << ",";
	}
	oss << "]";
	return oss.str();
}

void save_cache(const std::string& json) {
	auto folder = get_appdata_folder();
	auto path = folder + L"\\images.json";
	std::ofstream ofs(path, std::ios::trunc);
	if (ofs) {
		ofs << json;
	}
}

std::string load_cache() {
	auto folder = get_appdata_folder();
	auto path = folder + L"\\images.json";
	std::ifstream ifs(path);
	if (ifs) {
		std::ostringstream oss;
		oss << ifs.rdbuf();
		return oss.str();
	}
	return "";
}

std::string http_response(const std::string& body, const std::string& contentType = "application/json") {
	std::ostringstream oss;
	oss << "HTTP/1.1 200 OK\r\n";
	oss << "Content-Type: " << contentType << "\r\n";
	oss << "Access-Control-Allow-Origin: *\r\n";
	oss << "Content-Length: " << body.size() << "\r\n\r\n";
	oss << body;
	return oss.str();
}

std::string read_file(const std::string& path) {
	std::ifstream ifs(path, std::ios::binary);
	std::ostringstream oss;
	oss << ifs.rdbuf();
	return oss.str();
}

void serve_client(SOCKET client) {
	char buffer[8192] = {};
	int received = recv(client, buffer, sizeof(buffer), 0);
	if (received <= 0) {
		closesocket(client);
		return;
	}
	std::string req(buffer);
	std::string res;

	if (req.starts_with("GET /photos/")) {
		if (!all.empty())
		{
			auto start = req.find("GET /photos/") + 12;
			auto end = req.find(" ", start);
			std::string index = req.substr(start, end - start);
			auto idx = std::atoi(index.c_str()) % all.size();

			std::string full_path = all[idx];
			if (!full_path.empty()) {
				std::string body = read_file(full_path);
				if (!body.empty()) {
					std::string ext = full_path.substr(full_path.find_last_of('.') + 1);
					std::string type = "application/octet-stream";
					if (ext == "png") type = "image/png";
					else if (ext == "gif") type = "image/gif";
					else if (ext == "jpg" || ext == "jpeg") type = "image/jpeg";
					res = http_response(body, type);
				}
				else
					res = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
			}
			else {
				res = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
			}
		}
		else {
			res = "HTTP/1.1 500 Internal Server Error: no content loaded\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
		}
	}
	else {
		res = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
	}

	send(client, res.c_str(), (int)res.size(), 0);
	closesocket(client);
}

DWORD WINAPI server_thread(LPVOID) {
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);

	server = socket(AF_INET, SOCK_STREAM, 0);
	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(gPort);
	addr.sin_addr.s_addr = INADDR_ANY;
	bind(server, (sockaddr*)&addr, sizeof(addr));
	listen(server, SOMAXCONN);

	while (running) {
		SOCKET client = accept(server, NULL, NULL);
		if (client != INVALID_SOCKET) {
			std::thread(serve_client, client).detach();
		}
	}
	closesocket(server);
	WSACleanup();
	return 0;
}

void ShowMenu(HWND hwnd) {
	POINT pt;
	GetCursorPos(&pt);
	HMENU menu = CreatePopupMenu();
	//InsertMenu(menu, -1, MF_BYPOSITION, ID_TRAY_OPEN, L"Open First Image Folder");
	//InsertMenu(menu, -1, MF_BYPOSITION, ID_TRAY_BROWSER, L"Open Browser /images.json");
	InsertMenu(menu, -1, MF_BYPOSITION, ID_TRAY_EXIT, L"Exit");

	SetForegroundWindow(hwnd);
	TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
	DestroyMenu(menu);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_TRAYICON && lp == WM_RBUTTONUP) {
		ShowMenu(hwnd);
		return 0;
	}
	switch (msg) {
	case WM_COMMAND:
		switch (LOWORD(wp)) {
		//case ID_TRAY_OPEN:
		//	if (!gFolders.empty()) {
		//		ShellExecuteA(NULL, "open", gFolders[0].c_str(), NULL, NULL, SW_SHOWDEFAULT);
		//	}
		//	break;
		//case ID_TRAY_BROWSER: {
		//	std::ostringstream oss;
		//	oss << "http://localhost:" << gPort << "/images.json";
		//	ShellExecuteA(NULL, "open", oss.str().c_str(), NULL, NULL, SW_SHOWNORMAL);
		//	break;
		//}
		case ID_TRAY_EXIT:
			running = false;
			Shell_NotifyIcon(NIM_DELETE, &nid);
			PostQuitMessage(0);
			break;
		}
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	}
	return DefWindowProc(hwnd, msg, wp, lp);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
	read_config();

	WNDCLASS wc = { 0 };
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = L"ImageServerTray";
	RegisterClass(&wc);
	hWnd = CreateWindow(wc.lpszClassName, L"", 0, 0, 0, 0, 0, 0, 0, hInstance, 0);

	nid.cbSize = sizeof(nid);
	nid.hWnd = hWnd;
	nid.uID = 1;
	nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	nid.uCallbackMessage = WM_TRAYICON;
	nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TRAY));
	wcscpy_s(nid.szTip, L"Image Server Tray");
	Shell_NotifyIcon(NIM_ADD, &nid);

	auto json = load_cache();
	//if (json.empty())
	{
		for (const auto& folder : gFolders) {
			std::vector<std::string> files;
			try {
				for (auto& p : std::filesystem::recursive_directory_iterator(folder)) {
					if (p.is_regular_file()) {
						std::string ext = p.path().extension().string();
						if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif") {
							files.push_back(p.path().string());
						}
					}
				}
			}
			catch (...) {
				// skip if folder missing or inaccessible
			}
			all.insert(all.end(), files.begin(), files.end());
		}

		std::mt19937 rng(0); // Same each time
		std::shuffle(all.begin(), all.end(), rng);

		json = build_json_list(all);
		save_cache(json);
	}

	CreateThread(NULL, 0, server_thread, NULL, 0, NULL);

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;
}
