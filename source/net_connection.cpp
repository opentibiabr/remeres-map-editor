//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"
#include "net_connection.h"

// NetworkConnection
NetworkConnection::NetworkConnection() :
	service(nullptr), thread(), stopped(false) {
	//
}

NetworkConnection::~NetworkConnection() {
	stop();
}

NetworkConnection &NetworkConnection::getInstance() {
	static NetworkConnection connection;
	return connection;
}

bool NetworkConnection::start() {
	if (thread.joinable()) {
		if (stopped) {
			return false;
		}
		return true;
	}

	stopped = false;
	if (!service) {
		service = new asio::io_service;
	}

	thread = std::thread([this]() -> void {
		asio::io_service &serviceRef = *service;
		try {
			while (!stopped) {
				serviceRef.run_one();
				serviceRef.reset();
			}
		} catch (std::exception &e) {
			std::cout << e.what() << std::endl;
		}
	});
	return true;
}

void NetworkConnection::stop() {
	if (!service) {
		return;
	}

	service->stop();
	stopped = true;
	thread.join();

	delete service;
	service = nullptr;
}

asio::io_service &NetworkConnection::get_service() {
	return *service;
}
