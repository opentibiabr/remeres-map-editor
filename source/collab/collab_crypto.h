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

#ifndef RME_COLLAB_CRYPTO_H
#define RME_COLLAB_CRYPTO_H

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// libsodium wrapper: password -> keys, and one secretstream per direction.
namespace collab::crypto {

	constexpr size_t kSaltBytes = 16;
	constexpr size_t kKeyBytes = 32;
	constexpr size_t kHeaderBytes = 24; // crypto_secretstream_xchacha20poly1305_HEADERBYTES
	constexpr size_t kOverhead = 17; // crypto_secretstream_xchacha20poly1305_ABYTES

	using Salt = std::array<uint8_t, kSaltBytes>;
	using Key = std::array<uint8_t, kKeyBytes>;
	using Header = std::array<uint8_t, kHeaderBytes>;

	// Both directions of a session, derived from the password and the host's salt.
	struct SessionKeys {
		Key clientToServer {};
		Key serverToClient {};
		~SessionKeys();
	};

	// Call once at startup. Returns false if libsodium could not initialize.
	bool init();

	Salt randomSalt();

	// Argon2id (slow on purpose, ~0.1-0.5 s). Returns nullopt on allocation failure.
	std::optional<SessionKeys> deriveKeys(const std::string &password, const Salt &salt);

	// Encrypts messages in order; the receiver must decrypt them in the same order.
	class Pusher {
	public:
		explicit Pusher(const Key &key);
		~Pusher();
		const Header &header() const noexcept {
			return streamHeader;
		}
		std::vector<uint8_t> encrypt(const std::vector<uint8_t> &plain);

	private:
		struct State;
		std::unique_ptr<State> state;
		Header streamHeader {};
	};

	class Puller {
	public:
		Puller(const Key &key, const Header &header);
		~Puller();
		// nullopt on any authentication failure (wrong key, tampering, reorder, truncation).
		std::optional<std::vector<uint8_t>> decrypt(const uint8_t* cipher, size_t size);

	private:
		struct State;
		std::unique_ptr<State> state;
		bool valid = false;
	};

} // namespace collab::crypto

#endif
