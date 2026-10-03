// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "network.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <util/bmem.h>
#include <util/platform.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace bmagicam {

namespace {

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kNoSocket = INVALID_SOCKET;

void close_socket(Socket socket)
{
	closesocket(socket);
}

// Winsock counts its users, so every caller starts and stops it
struct Winsock {
	Winsock()
	{
		WSADATA data;
		WSAStartup(MAKEWORD(2, 2), &data);
	}
	~Winsock() { WSACleanup(); }
};
#else
using Socket = int;
constexpr Socket kNoSocket = -1;

void close_socket(Socket socket)
{
	close(socket);
}
#endif

} // namespace

std::string local_address_toward(const std::string &host)
{
#ifdef _WIN32
	const Winsock winsock;
#endif

	addrinfo hints = {};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	addrinfo *found = nullptr;
	if (getaddrinfo(host.c_str(), "4444", &hints, &found) != 0 || !found)
		return {};

	// Connecting a UDP socket sends nothing, but makes the system choose the local address for that route
	std::string address;
	const Socket socket = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (socket != kNoSocket) {
		sockaddr_in local = {};
		socklen_t length = sizeof(local);
		if (connect(socket, found->ai_addr, static_cast<socklen_t>(found->ai_addrlen)) == 0 &&
		    getsockname(socket, reinterpret_cast<sockaddr *>(&local), &length) == 0) {
			char text[INET_ADDRSTRLEN] = {};
			if (inet_ntop(AF_INET, &local.sin_addr, text, sizeof(text)))
				address = text;
		}
		close_socket(socket);
	}
	freeaddrinfo(found);
	return address;
}

std::string computer_name()
{
	std::string name;
#ifdef _WIN32
	wchar_t wide[MAX_COMPUTERNAME_LENGTH + 1] = {};
	DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
	if (GetComputerNameW(wide, &size)) {
		char *utf8 = nullptr;
		os_wcs_to_utf8_ptr(wide, size, &utf8);
		if (utf8)
			name = utf8;
		bfree(utf8);
	}
#else
	char text[256] = {};
	if (gethostname(text, sizeof(text) - 1) == 0)
		name = text;
#endif
	const size_t dot = name.find('.');
	if (dot != std::string::npos)
		name.erase(dot);
	return name.empty() ? "this computer" : name;
}

} // namespace bmagicam
