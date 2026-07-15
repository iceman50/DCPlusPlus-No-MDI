#include "testbase.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

#define private public
#include <dcpp/HashManager.h>
#undef private

#include <dcpp/ClientManager.h>
#include <dcpp/LogManager.h>
#include <dcpp/QueueManager.h>
#include <dcpp/SearchManager.h>
#include <dcpp/ScopedFunctor.h>
#include <dcpp/SettingsManager.h>
#include <dcpp/ShareManager.h>
#include <dcpp/SQLiteDB.h>
#include <dcpp/TimerManager.h>
#include <dcpp/Util.h>

using namespace dcpp;

namespace {

using Clock = std::chrono::steady_clock;

struct Stopwatch {
	Clock::time_point start = Clock::now();

	double elapsedMs() const {
		return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
	}
};

class BenchEnvironment {
public:
	explicit BenchEnvironment(const string& name) {
		const auto ticks = Clock::now().time_since_epoch().count();
		base = std::filesystem::temp_directory_path() /
			("dcpp-hashstore-bench-" + name + "-" + std::to_string(ticks));
		std::filesystem::remove_all(base);
		std::filesystem::create_directories(base);
		configPath = base.string() + PATH_SEPARATOR;

		Util::PathsMap paths;
		paths[Util::PATH_USER_CONFIG] = configPath;
		paths[Util::PATH_USER_LOCAL] = configPath;
		paths[Util::PATH_GLOBAL_CONFIG] = configPath;
		paths[Util::PATH_RESOURCES] = configPath;
		paths[Util::PATH_LOCALE] = configPath;
		paths[Util::PATH_DOWNLOADS] = configPath;
		paths[Util::PATH_FILE_LISTS] = configPath;
		paths[Util::PATH_HUB_LISTS] = configPath;
		paths[Util::PATH_NOTEPAD] = configPath + "Notepad.txt";
		Util::initialize(paths);

		SettingsManager::newInstance();
		LogManager::newInstance();
	}

	~BenchEnvironment() {
		LogManager::deleteInstance();
		SettingsManager::deleteInstance();
		std::filesystem::remove_all(base);
	}

	string path(const string& name) const {
		return configPath + name;
	}

private:
	std::filesystem::path base;
	string configPath;
};

class ExistingConfigEnvironment {
public:
	explicit ExistingConfigEnvironment(const string& configPath_) : configPath(configPath_) {
		if(!configPath.empty() && configPath[configPath.size() - 1] != PATH_SEPARATOR) {
			configPath += PATH_SEPARATOR;
		}

		Util::PathsMap paths;
		paths[Util::PATH_USER_CONFIG] = configPath;
		paths[Util::PATH_USER_LOCAL] = configPath;
		paths[Util::PATH_GLOBAL_CONFIG] = configPath;
		paths[Util::PATH_RESOURCES] = configPath;
		paths[Util::PATH_LOCALE] = configPath;
		paths[Util::PATH_DOWNLOADS] = configPath;
		paths[Util::PATH_FILE_LISTS] = configPath;
		paths[Util::PATH_HUB_LISTS] = configPath;
		paths[Util::PATH_NOTEPAD] = configPath + "Notepad.txt";
		Util::initialize(paths);

		SettingsManager::newInstance();
		LogManager::newInstance();
		SettingsManager::getInstance()->load(configPath + "DCPlusPlus.xml");
	}

	~ExistingConfigEnvironment() {
		LogManager::deleteInstance();
		SettingsManager::deleteInstance();
	}

private:
	string configPath;
};

class ShareRefreshEnvironment {
public:
	explicit ShareRefreshEnvironment(const string& configPath_) : configPath(configPath_) {
		if(!configPath.empty() && configPath[configPath.size() - 1] != PATH_SEPARATOR) {
			configPath += PATH_SEPARATOR;
		}

		Util::PathsMap paths;
		paths[Util::PATH_USER_CONFIG] = configPath;
		paths[Util::PATH_USER_LOCAL] = configPath;
		paths[Util::PATH_GLOBAL_CONFIG] = configPath;
		paths[Util::PATH_RESOURCES] = configPath;
		paths[Util::PATH_LOCALE] = configPath;
		paths[Util::PATH_DOWNLOADS] = configPath;
		paths[Util::PATH_FILE_LISTS] = configPath;
		paths[Util::PATH_HUB_LISTS] = configPath;
		paths[Util::PATH_NOTEPAD] = configPath + "Notepad.txt";
		Util::initialize(paths);

		SettingsManager::newInstance();
		LogManager::newInstance();
		TimerManager::newInstance();
		HashManager::newInstance();
		SearchManager::newInstance();
		ClientManager::newInstance();
		QueueManager::newInstance();
		ShareManager::newInstance();
	}

	~ShareRefreshEnvironment() {
		ShareManager::deleteInstance();
		QueueManager::deleteInstance();
		ClientManager::deleteInstance();
		SearchManager::deleteInstance();
		HashManager::getInstance()->shutdown();
		HashManager::deleteInstance();
		TimerManager::getInstance()->shutdown();
		TimerManager::deleteInstance();
		LogManager::deleteInstance();
		SettingsManager::deleteInstance();
	}

private:
	string configPath;
};

struct RootBytes {
	uint8_t data[TTHValue::BYTES];
};

RootBytes makeRootBytes(uint64_t value) {
	RootBytes root = {};
	for(size_t i = 0; i < TTHValue::BYTES; ++i) {
		root.data[i] = static_cast<uint8_t>((value >> ((i % 8) * 8)) + i * 37 + value * 13);
	}
	return root;
}

string makePath(size_t index, size_t dirs, bool active) {
	std::ostringstream os;
	os << (active ? "G:\\BenchActive\\" : "G:\\BenchOld\\")
		<< "Dir" << std::setw(6) << std::setfill('0') << (index % dirs)
		<< "\\File" << std::setw(9) << std::setfill('0') << index << ".bin";
	return os.str();
}

void createSchema(SQLiteDB& db) {
	db.execute(
		"CREATE TABLE trees ("
		"root BLOB PRIMARY KEY NOT NULL CHECK(length(root) = 24),"
		"size INTEGER NOT NULL CHECK(size >= 0),"
		"block_size INTEGER NOT NULL CHECK(block_size >= 1024),"
		"leaves BLOB"
		") WITHOUT ROWID;"
		"CREATE TABLE files ("
		"path TEXT PRIMARY KEY NOT NULL,"
		"size INTEGER NOT NULL CHECK(size >= 0),"
		"timestamp INTEGER NOT NULL CHECK(timestamp > 0),"
		"root BLOB NOT NULL CHECK(length(root) = 24),"
		"FOREIGN KEY(root) REFERENCES trees(root) ON UPDATE CASCADE ON DELETE CASCADE"
		") WITHOUT ROWID;"
		"CREATE INDEX idx_hash_files_root ON files(root);"
		"PRAGMA user_version = 2;"
	);
}

void populateStore(const string& dbPath, size_t files, size_t activeFiles) {
	SQLiteDB db(dbPath);
	createSchema(db);

	auto treeStmt = db.prepare("INSERT INTO trees(root, size, block_size, leaves) VALUES(?1, ?2, ?3, NULL)");
	auto fileStmt = db.prepare("INSERT INTO files(path, size, timestamp, root) VALUES(?1, ?2, ?3, ?4)");
	{
		SQLiteTransaction transaction(db);
		for(size_t i = 0; i < files; ++i) {
			const auto root = makeRootBytes(i + 1);
			const auto size = static_cast<int64_t>(4096 + (i % 1024));
			const auto active = i < activeFiles;
			const auto path = makePath(i, active ? 1000 : 4000, active);

			treeStmt.bind(1, root.data, TTHValue::BYTES);
			treeStmt.bind(2, size);
			treeStmt.bind(3, static_cast<int64_t>(1024));
			treeStmt.stepDone();
			treeStmt.reset();
			treeStmt.clearBindings();

			fileStmt.bind(1, path);
			fileStmt.bind(2, size);
			fileStmt.bind(3, static_cast<int64_t>(1700000000 + (i % 100000)));
			fileStmt.bind(4, root.data, TTHValue::BYTES);
			fileStmt.stepDone();
			fileStmt.reset();
			fileStmt.clearBindings();
		}
		transaction.commit();
	}
	db.execute("PRAGMA wal_checkpoint(TRUNCATE)");
}

struct LoadResult {
	double ms = 0;
	size_t files = 0;
	size_t dirs = 0;
	size_t trees = 0;
};

LoadResult loadHashStore() {
	Stopwatch timer;
	HashManager::HashStore store;
	store.load([](float) {});

	LoadResult result;
	result.ms = timer.elapsedMs();
	result.dirs = store.fileIndex.size();
	result.trees = store.treeIndex.size();
	for(const auto& i: store.fileIndex) {
		result.files += i.second.size();
	}
	return result;
}

struct LightweightFile {
	string fileName;
	TTHValue root;
	uint32_t timeStamp = 0;
};

LoadResult scanSql(const string& dbPath, const char* fileSql, const string& minPath = Util::emptyString, const string& maxPath = Util::emptyString, const char* pragmas = nullptr) {
	SQLiteDB db(dbPath);
	if(pragmas) {
		db.execute(pragmas);
	}

	Stopwatch timer;
	unordered_map<TTHValue, bool> roots;
	auto trees = db.prepare("SELECT root FROM trees");
	while(trees.step()) {
		if(trees.columnBytes(0) == TTHValue::BYTES) {
			roots.emplace(TTHValue(static_cast<const uint8_t*>(trees.columnBlob(0))), true);
		}
	}

	unordered_map<string, vector<LightweightFile>> files;
	auto stmt = db.prepare(fileSql);
	if(!minPath.empty()) {
		stmt.bind(1, minPath);
		stmt.bind(2, maxPath);
	}

	size_t fileCount = 0;
	while(stmt.step()) {
		if(stmt.columnBytes(2) != TTHValue::BYTES) {
			continue;
		}

		const auto root = TTHValue(static_cast<const uint8_t*>(stmt.columnBlob(2)));
		if(roots.find(root) == roots.end()) {
			continue;
		}

		const auto path = stmt.columnText(0);
		files[Util::getFilePath(path)].push_back(LightweightFile {
			Util::getFileName(path),
			root,
			static_cast<uint32_t>(stmt.columnInt64(1))
		});
		fileCount++;
	}

	LoadResult result;
	result.ms = timer.elapsedMs();
	result.files = fileCount;
	result.dirs = files.size();
	result.trees = roots.size();
	return result;
}

void printResult(const string& label, const LoadResult& result) {
	std::cout << std::left << std::setw(34) << label
		<< " ms=" << std::right << std::setw(9) << std::fixed << std::setprecision(2) << result.ms
		<< " files=" << result.files
		<< " dirs=" << result.dirs
		<< " trees=" << result.trees
		<< std::endl;
}

int64_t scalarInt64(SQLiteDB& db, const char* sql) {
	auto stmt = db.prepare(sql);
	return stmt.step() ? stmt.columnInt64(0) : 0;
}

struct StoreStats {
	int64_t files = 0;
	int64_t trees = 0;
	int64_t pageCount = 0;
	int64_t freePages = 0;
	int64_t pageSize = 0;
	int64_t userVersion = 0;
	int64_t quickCheckMs = 0;
	string quickCheck;
};

StoreStats inspectStore(const string& dbPath, bool runQuickCheck) {
	SQLiteDB db(dbPath, true);
	StoreStats stats;
	stats.files = scalarInt64(db, "SELECT COUNT(*) FROM files");
	stats.trees = scalarInt64(db, "SELECT COUNT(*) FROM trees");
	stats.pageCount = scalarInt64(db, "PRAGMA page_count");
	stats.freePages = scalarInt64(db, "PRAGMA freelist_count");
	stats.pageSize = scalarInt64(db, "PRAGMA page_size");
	stats.userVersion = scalarInt64(db, "PRAGMA user_version");

	if(runQuickCheck) {
		Stopwatch timer;
		auto stmt = db.prepare("PRAGMA quick_check");
		stats.quickCheck = stmt.step() ? stmt.columnText(0) : "no result";
		stats.quickCheckMs = static_cast<int64_t>(timer.elapsedMs());
	}

	return stats;
}

void printStats(const StoreStats& stats) {
	std::cout << "files=" << stats.files
		<< " trees=" << stats.trees
		<< " userVersion=" << stats.userVersion
		<< " pages=" << stats.pageCount
		<< " freePages=" << stats.freePages
		<< " pageSize=" << stats.pageSize
		<< std::endl;
	if(!stats.quickCheck.empty()) {
		std::cout << "quick_check=" << stats.quickCheck << " ms=" << stats.quickCheckMs << std::endl;
	}
}

void runScenario(const string& name, size_t totalFiles, size_t activeFiles) {
	BenchEnvironment env(name);
	const auto dbPath = env.path("HashStore.sqlite3");

	Stopwatch createTimer;
	populateStore(dbPath, totalFiles, activeFiles);
	const auto createMs = createTimer.elapsedMs();
	const auto dbBytes = std::filesystem::file_size(dbPath);

	std::cout << "\nScenario: " << name
		<< " total=" << totalFiles
		<< " active=" << activeFiles
		<< " dbMiB=" << std::fixed << std::setprecision(2) << (dbBytes / 1024.0 / 1024.0)
		<< " createMs=" << createMs
		<< std::endl;

	printResult("HashStore::load current", loadHashStore());
	printResult("SQL mimic ORDER BY path", scanSql(dbPath, "SELECT path, timestamp, root FROM files ORDER BY path"));
	printResult("SQL mimic no ORDER BY", scanSql(dbPath, "SELECT path, timestamp, root FROM files"));
	printResult("SQL mimic cache 64MiB", scanSql(dbPath, "SELECT path, timestamp, root FROM files ORDER BY path", Util::emptyString, Util::emptyString, "PRAGMA cache_size=-65536;"));
	printResult("SQL mimic mmap 256MiB", scanSql(dbPath, "SELECT path, timestamp, root FROM files ORDER BY path", Util::emptyString, Util::emptyString, "PRAGMA mmap_size=268435456;"));
	printResult("SQL mimic cache+mmap", scanSql(dbPath, "SELECT path, timestamp, root FROM files ORDER BY path", Util::emptyString, Util::emptyString, "PRAGMA cache_size=-65536;PRAGMA mmap_size=268435456;"));

	if(activeFiles < totalFiles) {
		printResult("SQL active-prefix only", scanSql(dbPath,
			"SELECT path, timestamp, root FROM files WHERE path >= ?1 AND path < ?2 ORDER BY path",
			"G:\\BenchActive\\", "G:\\BenchActive]"));
	}
}

}

TEST(HashStoreBenchmark, DISABLED_startup_scale)
{
	runScenario("100k-active", 100000, 100000);
	runScenario("500k-active", 500000, 500000);
	runScenario("500k-total-100k-active", 500000, 100000);
}

TEST(HashStoreBenchmark, DISABLED_existing_store)
{
	const auto dbPathEnv = std::getenv("HASHSTORE_BENCH_PATH");
	ASSERT_TRUE(dbPathEnv && *dbPathEnv) << "Set HASHSTORE_BENCH_PATH to HashStore.sqlite3";

	const string dbPath = dbPathEnv;
	const auto separator = dbPath.find_last_of("\\/");
	ASSERT_NE(string::npos, separator);
	const auto configPath = dbPath.substr(0, separator + 1);

	ExistingConfigEnvironment env(configPath);
	std::cout << "\nExisting store: " << dbPath << std::endl;
	std::cout << "HashDbVerifyStartup=" << (SETTING(HASH_DB_VERIFY_STARTUP) ? "true" : "false")
		<< " HashDbWriteBatchSize=" << SETTING(HASH_DB_WRITE_BATCH_SIZE)
		<< std::endl;

	printStats(inspectStore(dbPath, SETTING(HASH_DB_VERIFY_STARTUP)));
	printResult("HashStore::load existing", loadHashStore());
	printResult("SQL mimic ORDER BY path", scanSql(dbPath, "SELECT path, timestamp, root FROM files ORDER BY path"));
	printResult("SQL mimic no ORDER BY", scanSql(dbPath, "SELECT path, timestamp, root FROM files"));
	printResult("SQL mimic mmap 256MiB", scanSql(dbPath, "SELECT path, timestamp, root FROM files ORDER BY path", Util::emptyString, Util::emptyString, "PRAGMA mmap_size=268435456;"));
}

TEST(HashStoreBenchmark, DISABLED_existing_startup_steps)
{
	const auto dbPathEnv = std::getenv("HASHSTORE_BENCH_PATH");
	ASSERT_TRUE(dbPathEnv && *dbPathEnv) << "Set HASHSTORE_BENCH_PATH to HashStore.sqlite3";

	const string dbPath = dbPathEnv;
	const auto separator = dbPath.find_last_of("\\/");
	ASSERT_NE(string::npos, separator);
	const auto configPath = dbPath.substr(0, separator + 1);

	ShareRefreshEnvironment env(configPath);
	Stopwatch settingsTimer;
	SettingsManager::getInstance()->load(configPath + "DCPlusPlus.xml");
	const auto settingsMs = settingsTimer.elapsedMs();

	Stopwatch hashTimer;
	HashManager::getInstance()->startup([](float) {});
	const auto hashMs = hashTimer.elapsedMs();

	Stopwatch shareTimer;
	ShareManager::getInstance()->refresh(true, false, true, [](float) {});
	const auto shareMs = shareTimer.elapsedMs();

	std::cout << "\nExisting startup steps: " << configPath << std::endl;
	std::cout << "settingsLoadMs=" << settingsMs
		<< " hashStartupMs=" << hashMs
		<< " shareRefreshMs=" << shareMs
		<< " sharedFiles=" << ShareManager::getInstance()->getSharedFiles()
		<< " shareSize=" << ShareManager::getInstance()->getShareSize()
		<< std::endl;
}
