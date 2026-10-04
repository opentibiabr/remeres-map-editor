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
#include "collab_protocol.h"
#include "collab_session.h"

#include <cstdio>
#include <random>

namespace collab {

	namespace {
		int failures = 0;

		void check(bool ok, const char* what) {
			if (!ok) {
				std::fprintf(stderr, "collab selfcheck FAILED: %s\n", what);
				++failures;
			}
		}

		template <typename F>
		bool throwsProtocolError(F fn) {
			try {
				fn();
			} catch (const ProtocolError &) {
				return true;
			}
			return false;
		}

		void checkReaderWriter() {
			ByteWriter w;
			w.u8(7);
			w.u16(0xBEEF);
			w.u32(0xDEADBEEF);
			w.str("hello");

			ByteReader r(w.buffer);
			check(r.u8() == 7 && r.u16() == 0xBEEF && r.u32() == 0xDEADBEEF && r.str(16) == "hello" && r.remaining() == 0, "reader/writer round trip");

			// Every truncation of a valid buffer must throw, never read out of bounds.
			for (size_t cut = 0; cut < w.buffer.size(); ++cut) {
				std::vector<uint8_t> truncated(w.buffer.begin(), w.buffer.begin() + cut);
				check(throwsProtocolError([&] {
					ByteReader t(truncated);
					t.u8();
					t.u16();
					t.u32();
					t.str(16);
				}),
					"truncated buffer rejected");
			}

			check(throwsProtocolError([&] {
				ByteReader t(w.buffer);
				t.u8();
				t.u16();
				t.u32();
				t.str(2); // longer than the cap
			}),
				"string cap enforced");

			// Garbage: either parses or throws ProtocolError, nothing else.
			std::mt19937 rng(1234);
			for (int i = 0; i < 2000; ++i) {
				std::vector<uint8_t> garbage(rng() % 64);
				for (auto &b : garbage) {
					b = static_cast<uint8_t>(rng());
				}
				try {
					ByteReader g(garbage);
					while (g.remaining() > 0) {
						switch (g.u8() % 4) {
							case 0:
								g.u16();
								break;
							case 1:
								g.u32();
								break;
							case 2:
								g.str(32);
								break;
							default:
								g.skip(g.u8());
								break;
						}
					}
				} catch (const ProtocolError &) {
				}
			}
		}

		void checkCrypto() {
			check(crypto::init(), "sodium init");

			const crypto::Salt salt = crypto::randomSalt();
			auto a = crypto::deriveKeys("correct horse", salt);
			auto b = crypto::deriveKeys("correct horse", salt);
			auto c = crypto::deriveKeys("battery staple", salt);
			check(a && b && c, "key derivation");
			if (!a || !b || !c) {
				return;
			}
			check(a->clientToServer == b->clientToServer && a->serverToClient == b->serverToClient, "same password, same keys");
			check(a->clientToServer != c->clientToServer, "other password, other keys");
			check(a->clientToServer != a->serverToClient, "directions use different keys");

			crypto::Pusher push(a->clientToServer);
			crypto::Puller pull(b->clientToServer, push.header());
			const std::vector<uint8_t> m1 = makeFrame(Msg::Chat, { 1, 2, 3 });
			const std::vector<uint8_t> m2 = makeFrame(Msg::Cursor, { 9 });
			const auto c1 = push.encrypt(m1);
			const auto c2 = push.encrypt(m2);
			check(c1.size() == m1.size() + crypto::kOverhead, "ciphertext overhead");

			auto p1 = pull.decrypt(c1.data(), c1.size());
			auto p2 = pull.decrypt(c2.data(), c2.size());
			check(p1 && *p1 == m1 && p2 && *p2 == m2, "stream round trip");

			// Wrong key, tampering and reordering must all fail authentication.
			crypto::Puller wrongKey(c->clientToServer, push.header());
			check(!wrongKey.decrypt(c1.data(), c1.size()), "wrong key rejected");

			crypto::Pusher push2(a->clientToServer);
			crypto::Puller pull2(a->clientToServer, push2.header());
			auto d1 = push2.encrypt(m1);
			d1[d1.size() / 2] ^= 0x01;
			check(!pull2.decrypt(d1.data(), d1.size()), "tampering rejected");

			crypto::Pusher push3(a->clientToServer);
			crypto::Puller pull3(a->clientToServer, push3.header());
			push3.encrypt(m1);
			auto e2 = push3.encrypt(m2);
			check(!pull3.decrypt(e2.data(), e2.size()), "reordering rejected");

			check(!pull.decrypt(c1.data(), 3), "short ciphertext rejected");
		}

		void checkSanitize() {
			check(sanitizeText("  Ana  ", 32) == "Ana", "trim");
			check(sanitizeText("a\nb\tc", 32) == "a b c", "control characters");
			check(sanitizeText("abcdef", 3) == "abc", "length cap");
			check(sanitizeText(std::string("\xff\xfe", 2), 32).empty(), "invalid UTF-8 rejected");
			check(sanitizeText("", 32).empty(), "empty stays empty");
		}
	}

	bool selfCheck() {
		failures = 0;
		checkReaderWriter();
		checkCrypto();
		checkSanitize();
		if (failures == 0) {
			std::fprintf(stdout, "collab selfcheck OK\n");
		}
		return failures == 0;
	}

} // namespace collab
