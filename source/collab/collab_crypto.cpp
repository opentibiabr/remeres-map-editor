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

#include "collab_crypto.h"

#include <sodium.h>

namespace collab::crypto {

	static_assert(kSaltBytes == crypto_pwhash_SALTBYTES);
	static_assert(kKeyBytes == crypto_secretstream_xchacha20poly1305_KEYBYTES);
	static_assert(kHeaderBytes == crypto_secretstream_xchacha20poly1305_HEADERBYTES);
	static_assert(kOverhead == crypto_secretstream_xchacha20poly1305_ABYTES);

	struct Pusher::State {
		crypto_secretstream_xchacha20poly1305_state stream;
	};

	struct Puller::State {
		crypto_secretstream_xchacha20poly1305_state stream;
	};

	SessionKeys::~SessionKeys() {
		sodium_memzero(clientToServer.data(), clientToServer.size());
		sodium_memzero(serverToClient.data(), serverToClient.size());
	}

	bool init() {
		return sodium_init() >= 0;
	}

	Salt randomSalt() {
		Salt salt;
		randombytes_buf(salt.data(), salt.size());
		return salt;
	}

	std::optional<SessionKeys> deriveKeys(const std::string &password, const Salt &salt) {
		Key master;
		if (crypto_pwhash(master.data(), master.size(), password.data(), password.size(), salt.data(), crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE, crypto_pwhash_ALG_ARGON2ID13) != 0) {
			return std::nullopt;
		}

		SessionKeys keys;
		// The context is exactly 8 bytes, as crypto_kdf requires.
		crypto_kdf_derive_from_key(keys.clientToServer.data(), keys.clientToServer.size(), 1, "RMECOLAB", master.data());
		crypto_kdf_derive_from_key(keys.serverToClient.data(), keys.serverToClient.size(), 2, "RMECOLAB", master.data());
		sodium_memzero(master.data(), master.size());
		return keys;
	}

	Pusher::Pusher(const Key &key) :
		state(std::make_unique<State>()) {
		crypto_secretstream_xchacha20poly1305_init_push(&state->stream, streamHeader.data(), key.data());
	}

	Pusher::~Pusher() {
		if (state) {
			sodium_memzero(&state->stream, sizeof(state->stream));
		}
	}

	std::vector<uint8_t> Pusher::encrypt(const std::vector<uint8_t> &plain) {
		std::vector<uint8_t> cipher(plain.size() + kOverhead);
		unsigned long long written = 0;
		crypto_secretstream_xchacha20poly1305_push(&state->stream, cipher.data(), &written, plain.data(), plain.size(), nullptr, 0, crypto_secretstream_xchacha20poly1305_TAG_MESSAGE);
		cipher.resize(static_cast<size_t>(written));
		return cipher;
	}

	Puller::Puller(const Key &key, const Header &header) :
		state(std::make_unique<State>()) {
		valid = crypto_secretstream_xchacha20poly1305_init_pull(&state->stream, header.data(), key.data()) == 0;
	}

	Puller::~Puller() {
		if (state) {
			sodium_memzero(&state->stream, sizeof(state->stream));
		}
	}

	std::optional<std::vector<uint8_t>> Puller::decrypt(const uint8_t* cipher, size_t size) {
		if (!valid || size < kOverhead) {
			return std::nullopt;
		}
		std::vector<uint8_t> plain(size - kOverhead);
		unsigned long long written = 0;
		unsigned char tag = 0;
		if (crypto_secretstream_xchacha20poly1305_pull(&state->stream, plain.data(), &written, &tag, cipher, size, nullptr, 0) != 0) {
			return std::nullopt;
		}
		plain.resize(static_cast<size_t>(written));
		return plain;
	}

} // namespace collab::crypto
