#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace world_files {
	using Revision = std::optional<std::string>; // absent file is distinct from empty content
	struct Change {
		std::filesystem::path file;
		Revision before, after;
	};
	struct Publication {
		std::filesystem::path catalog, root;
		std::vector<Change> changes; // dependency order; catalog last
		std::map<std::filesystem::path, Revision> guards;
	};
	struct MapSaveFile {
		std::filesystem::path target;
		bool existed = false;
	};
	struct MapSaveRecovery {
		std::filesystem::path catalog;
		std::vector<MapSaveFile> files;
		bool writing = false;
		bool committed = false;
	};
	bool prepareMapSave(const std::filesystem::path &marker, MapSaveRecovery &recovery, bool &recorded, std::string &error);
	bool markMapSaveCommitted(const std::filesystem::path &marker, MapSaveRecovery &recovery, std::string &error);
	bool readMapSave(const std::filesystem::path &marker, MapSaveRecovery &recovery, std::string &error);
	bool restoreMapSave(const MapSaveRecovery &recovery, std::string &error);
	bool clearMapSave(const std::filesystem::path &marker, std::string &error);
	bool revision(const std::filesystem::path &file, Revision &result, std::string &error);
	bool publish(const Publication &publication, std::string &error);
	bool recover(const std::filesystem::path &catalog, const std::filesystem::path &root, bool rollback, std::string &error);
	bool beginCoordination(const std::filesystem::path &catalog, std::string &error);
	bool endCoordination(const std::filesystem::path &catalog, std::string &error);
	bool recoverCoordination(const std::filesystem::path &catalog, const std::filesystem::path &root, bool rollback, std::string &error);
	bool transactionPending(const std::filesystem::path &catalog);
	bool coordinatedPending(const std::filesystem::path &catalog);
	bool pending(const std::filesystem::path &catalog);
#ifdef WORLD_FILES_TEST_HOOKS
	// Fault injection exists only in the standalone file-contract test target.
	void setTestHook(void (*hook)(const char*, const std::filesystem::path &));
#endif

	// Cooperating readers and publishers share an OS lock. A revision token also
	// covers the first publication, before a lock file exists in a read-only pack.
	class ReadGuard {
	public:
		ReadGuard(const std::filesystem::path &catalog, std::string &error);
		~ReadGuard();
		bool valid() const;
		bool unchanged(std::string &error) const;
		ReadGuard(const ReadGuard &) = delete;
		ReadGuard &operator=(const ReadGuard &) = delete;

	private:
		struct State;
		std::unique_ptr<State> state;
	};
}
