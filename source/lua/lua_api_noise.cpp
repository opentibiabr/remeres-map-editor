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
#include "lua_api_noise.h"
#include "FastNoiseLite.h"

#include <unordered_map>
#include <mutex>
#include <cmath>

namespace LuaAPI {

	// Thread-local noise generator cache for better performance
	// Each seed gets its own generator instance
	class NoiseGeneratorCache {
	public:
		FastNoiseLite &getGenerator(int seed) {
			std::scoped_lock lock(mutex_);
			auto it = generators_.find(seed);
			if (it == generators_.end()) {
				auto &gen = generators_[seed];
				gen.SetSeed(seed);
				return gen;
			}
			return it->second;
		}

		void clear() {
			std::scoped_lock lock(mutex_);
			generators_.clear();
		}

	private:
		std::unordered_map<int, FastNoiseLite> generators_;
		std::mutex mutex_;
	};

	static NoiseGeneratorCache &noiseCache() {
		static NoiseGeneratorCache instance;
		return instance;
	}

	static FastNoiseLite createNoiseGenerator(int seed, FastNoiseLite::NoiseType type, float frequency = 0.01f) {
		FastNoiseLite noise;
		noise.SetSeed(seed);
		noise.SetNoiseType(type);
		noise.SetFrequency(frequency);
		return noise;
	}

	static FastNoiseLite::NoiseType resolveNoiseType(const std::string &name) {
		static const std::unordered_map<std::string, FastNoiseLite::NoiseType> types = {
			{ "perlin", FastNoiseLite::NoiseType_Perlin },
			{ "simplex", FastNoiseLite::NoiseType_OpenSimplex2 },
			{ "opensimplex", FastNoiseLite::NoiseType_OpenSimplex2 },
			{ "value", FastNoiseLite::NoiseType_Value },
			{ "cellular", FastNoiseLite::NoiseType_Cellular },
		};
		auto it = types.find(name);
		return it != types.end() ? it->second : FastNoiseLite::NoiseType_OpenSimplex2;
	}

	static FastNoiseLite::CellularDistanceFunction resolveCellularDistance(const std::string &name) {
		static const std::unordered_map<std::string, FastNoiseLite::CellularDistanceFunction> types = {
			{ "euclidean", FastNoiseLite::CellularDistanceFunction_EuclideanSq },
			{ "euclideanSq", FastNoiseLite::CellularDistanceFunction_EuclideanSq },
			{ "manhattan", FastNoiseLite::CellularDistanceFunction_Manhattan },
			{ "hybrid", FastNoiseLite::CellularDistanceFunction_Hybrid },
		};
		auto it = types.find(name);
		return it != types.end() ? it->second : FastNoiseLite::CellularDistanceFunction_EuclideanSq;
	}

	static FastNoiseLite::CellularReturnType resolveCellularReturn(const std::string &name) {
		static const std::unordered_map<std::string, FastNoiseLite::CellularReturnType> types = {
			{ "cellValue", FastNoiseLite::CellularReturnType_CellValue },
			{ "distance", FastNoiseLite::CellularReturnType_Distance },
			{ "distance2", FastNoiseLite::CellularReturnType_Distance2 },
			{ "distance2Add", FastNoiseLite::CellularReturnType_Distance2Add },
			{ "distance2Sub", FastNoiseLite::CellularReturnType_Distance2Sub },
			{ "distance2Mul", FastNoiseLite::CellularReturnType_Distance2Mul },
			{ "distance2Div", FastNoiseLite::CellularReturnType_Distance2Div },
		};
		auto it = types.find(name);
		return it != types.end() ? it->second : FastNoiseLite::CellularReturnType_Distance;
	}

	static FastNoiseLite::DomainWarpType resolveDomainWarpType(const std::string &name) {
		static const std::unordered_map<std::string, FastNoiseLite::DomainWarpType> types = {
			{ "simplex", FastNoiseLite::DomainWarpType_OpenSimplex2 },
			{ "opensimplex", FastNoiseLite::DomainWarpType_OpenSimplex2 },
			{ "simplexReduced", FastNoiseLite::DomainWarpType_OpenSimplex2Reduced },
			{ "basic", FastNoiseLite::DomainWarpType_BasicGrid },
		};
		auto it = types.find(name);
		return it != types.end() ? it->second : FastNoiseLite::DomainWarpType_OpenSimplex2;
	}

	static float cellularNoise(float x, float y, sol::optional<int> seed, sol::optional<float> frequency, sol::optional<std::string> distanceFunc, sol::optional<std::string> returnType) {
		int s = seed.value_or(1337);
		float freq = frequency.value_or(0.01f);

		FastNoiseLite noise = createNoiseGenerator(s, FastNoiseLite::NoiseType_Cellular, freq);
		noise.SetCellularDistanceFunction(resolveCellularDistance(distanceFunc.value_or("euclidean")));
		noise.SetCellularReturnType(resolveCellularReturn(returnType.value_or("distance")));

		return noise.GetNoise(x, y);
	}

	static void configureFbm(FastNoiseLite &noise, const sol::table &opts) {
		noise.SetFrequency(opts.get_or(std::string("frequency"), 0.01f));
		noise.SetFractalOctaves(opts.get_or(std::string("octaves"), 4));
		noise.SetFractalLacunarity(opts.get_or(std::string("lacunarity"), 2.0f));
		noise.SetFractalGain(opts.get_or(std::string("gain"), 0.5f));
		std::string noiseType = opts.get_or<std::string>(std::string("noiseType"), "simplex");
		noise.SetNoiseType(resolveNoiseType(noiseType));
	}

	static float fbmNoise(float x, float y, sol::optional<int> seed, sol::optional<sol::table> options) {
		FastNoiseLite noise;
		noise.SetSeed(seed.value_or(1337));
		noise.SetFractalType(FastNoiseLite::FractalType_FBm);

		if (options) {
			configureFbm(noise, *options);
		} else {
			noise.SetFrequency(0.01f);
			noise.SetFractalOctaves(4);
			noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
		}
		return noise.GetNoise(x, y);
	}

	static float fbmNoise3d(float x, float y, float z, sol::optional<int> seed, sol::optional<sol::table> options) {
		FastNoiseLite noise;
		noise.SetSeed(seed.value_or(1337));
		noise.SetFractalType(FastNoiseLite::FractalType_FBm);

		if (options) {
			configureFbm(noise, *options);
		} else {
			noise.SetFrequency(0.01f);
			noise.SetFractalOctaves(4);
			noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
		}
		return noise.GetNoise(x, y, z);
	}

	static sol::object warpNoise(float x, float y, sol::optional<int> seed, sol::optional<sol::table> options, sol::this_state s) {
		FastNoiseLite noise;
		noise.SetSeed(seed.value_or(1337));

		float amplitude = 30.0f;
		float frequency = 0.01f;

		if (options) {
			sol::table opts = *options;
			amplitude = opts.get_or(std::string("amplitude"), 30.0f);
			frequency = opts.get_or(std::string("frequency"), 0.01f);
			std::string warpType = opts.get_or<std::string>(std::string("type"), "simplex");
			noise.SetDomainWarpType(resolveDomainWarpType(warpType));
		} else {
			noise.SetDomainWarpType(FastNoiseLite::DomainWarpType_OpenSimplex2);
		}

		noise.SetDomainWarpAmp(amplitude);
		noise.SetFrequency(frequency);
		noise.DomainWarp(x, y);

		sol::state_view lua(s);
		sol::table result = lua.create_table();
		result["x"] = x;
		result["y"] = y;
		return result;
	}

	static void configureGridNoise(FastNoiseLite &noise, const sol::table &opts) {
		noise.SetSeed(opts.get_or(std::string("seed"), 1337));
		noise.SetFrequency(opts.get_or(std::string("frequency"), 0.01f));
		noise.SetNoiseType(resolveNoiseType(opts.get_or<std::string>(std::string("noiseType"), "simplex")));

		std::string fractal = opts.get_or<std::string>(std::string("fractal"), "none");
		if (fractal == "fbm") {
			noise.SetFractalType(FastNoiseLite::FractalType_FBm);
			noise.SetFractalOctaves(opts.get_or(std::string("octaves"), 4));
			noise.SetFractalLacunarity(opts.get_or(std::string("lacunarity"), 2.0f));
			noise.SetFractalGain(opts.get_or(std::string("gain"), 0.5f));
		} else if (fractal == "ridged") {
			noise.SetFractalType(FastNoiseLite::FractalType_Ridged);
			noise.SetFractalOctaves(opts.get_or(std::string("octaves"), 4));
		}
	}

	static sol::table generateGridNoise(int x1, int y1, int x2, int y2, sol::optional<sol::table> options, sol::this_state s) {
		sol::state_view lua(s);
		sol::table result = lua.create_table();

		FastNoiseLite noise;
		if (options) {
			configureGridNoise(noise, *options);
		} else {
			noise.SetSeed(1337);
			noise.SetFrequency(0.01f);
			noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
		}

		for (int y = y1; y <= y2; ++y) {
			sol::table row = lua.create_table();
			for (int x = x1; x <= x2; ++x) {
				row[x - x1 + 1] = noise.GetNoise((float)x, (float)y);
			}
			result[y - y1 + 1] = row;
		}
		return result;
	}

	void registerNoise(sol::state &lua) {
		sol::table noiseTable = lua.create_table();

		noiseTable.set_function("perlin", [](float x, float y, sol::optional<int> seed, sol::optional<float> frequency) -> float {
			return createNoiseGenerator(seed.value_or(1337), FastNoiseLite::NoiseType_Perlin, frequency.value_or(0.01f)).GetNoise(x, y);
		});

		noiseTable.set_function("perlin3d", [](float x, float y, float z, sol::optional<int> seed, sol::optional<float> frequency) -> float {
			return createNoiseGenerator(seed.value_or(1337), FastNoiseLite::NoiseType_Perlin, frequency.value_or(0.01f)).GetNoise(x, y, z);
		});

		noiseTable.set_function("simplex", [](float x, float y, sol::optional<int> seed, sol::optional<float> frequency) -> float {
			return createNoiseGenerator(seed.value_or(1337), FastNoiseLite::NoiseType_OpenSimplex2, frequency.value_or(0.01f)).GetNoise(x, y);
		});

		noiseTable.set_function("simplex3d", [](float x, float y, float z, sol::optional<int> seed, sol::optional<float> frequency) -> float {
			return createNoiseGenerator(seed.value_or(1337), FastNoiseLite::NoiseType_OpenSimplex2, frequency.value_or(0.01f)).GetNoise(x, y, z);
		});

		noiseTable.set_function("simplexSmooth", [](float x, float y, sol::optional<int> seed, sol::optional<float> frequency) -> float {
			return createNoiseGenerator(seed.value_or(1337), FastNoiseLite::NoiseType_OpenSimplex2S, frequency.value_or(0.01f)).GetNoise(x, y);
		});

		noiseTable.set_function("cellular", cellularNoise);

		noiseTable.set_function("cellular3d", [](float x, float y, float z, sol::optional<int> seed, sol::optional<float> frequency) -> float {
			return createNoiseGenerator(seed.value_or(1337), FastNoiseLite::NoiseType_Cellular, frequency.value_or(0.01f)).GetNoise(x, y, z);
		});

		noiseTable.set_function("value", [](float x, float y, sol::optional<int> seed, sol::optional<float> frequency) -> float {
			return createNoiseGenerator(seed.value_or(1337), FastNoiseLite::NoiseType_Value, frequency.value_or(0.01f)).GetNoise(x, y);
		});

		noiseTable.set_function("valueCubic", [](float x, float y, sol::optional<int> seed, sol::optional<float> frequency) -> float {
			return createNoiseGenerator(seed.value_or(1337), FastNoiseLite::NoiseType_ValueCubic, frequency.value_or(0.01f)).GetNoise(x, y);
		});

		noiseTable.set_function("fbm", fbmNoise);
		noiseTable.set_function("fbm3d", fbmNoise3d);

		noiseTable.set_function("ridged", [](float x, float y, sol::optional<int> seed, sol::optional<sol::table> options) -> float {
			FastNoiseLite noise;
			noise.SetSeed(seed.value_or(1337));
			noise.SetFractalType(FastNoiseLite::FractalType_Ridged);
			noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);

			if (options) {
				sol::table opts = *options;
				noise.SetFrequency(opts.get_or(std::string("frequency"), 0.01f));
				noise.SetFractalOctaves(opts.get_or(std::string("octaves"), 4));
				noise.SetFractalLacunarity(opts.get_or(std::string("lacunarity"), 2.0f));
				noise.SetFractalGain(opts.get_or(std::string("gain"), 0.5f));
			} else {
				noise.SetFrequency(0.01f);
				noise.SetFractalOctaves(4);
			}
			return noise.GetNoise(x, y);
		});

		noiseTable.set_function("warp", warpNoise);

		noiseTable.set_function("normalize", [](float value, sol::optional<float> minVal, sol::optional<float> maxVal) {
			float min = minVal.value_or(0.0f);
			float mx = maxVal.value_or(1.0f);
			float normalized = (value + 1.0f) * 0.5f;
			return std::lerp(min, mx, normalized);
		});

		noiseTable.set_function("threshold", [](float value, float threshold) {
			return value >= threshold;
		});

		noiseTable.set_function("map", [](float value, float inMin, float inMax, float outMin, float outMax) {
			float t = (value - inMin) / (inMax - inMin);
			return std::lerp(outMin, outMax, t);
		});

		noiseTable.set_function("clamp", [](float value, float min, float max) {
			if (value < min) {
				return min;
			}
			if (value > max) {
				return max;
			}
			return value;
		});

		noiseTable.set_function("lerp", [](float a, float b, float t) {
			return std::lerp(a, b, t);
		});

		noiseTable.set_function("smoothstep", [](float edge0, float edge1, float x) {
			float t = (x - edge0) / (edge1 - edge0);
			if (t < 0.0f) {
				t = 0.0f;
			}
			if (t > 1.0f) {
				t = 1.0f;
			}
			return t * t * (3.0f - 2.0f * t);
		});

		noiseTable.set_function("clearCache", []() {
			noiseCache().clear();
		});

		noiseTable.set_function("generateGrid", generateGridNoise);

		lua["noise"] = noiseTable;
	}

} // namespace LuaAPI
