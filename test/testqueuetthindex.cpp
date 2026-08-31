/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "testbase.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>

#include <dcpp/ClientManager.h>
#include <dcpp/File.h>
#include <dcpp/LogManager.h>
#include <dcpp/QueueManager.h>
#include <dcpp/SearchManager.h>
#include <dcpp/SettingsManager.h>
#include <dcpp/SimpleXML.h>
#include <dcpp/Streams.h>
#include <dcpp/TimerManager.h>
#include <dcpp/version.h>

using namespace dcpp;

namespace {

TTHValue makeTTH(uint32_t value) {
	uint8_t data[TTHValue::BYTES] = { 0 };
	for(size_t i = 0; i < sizeof(value); ++i) {
		data[i] = static_cast<uint8_t>(value >> (i * 8));
	}
	return TTHValue(data);
}

UserPtr makeUser(uint8_t value) {
	uint8_t data[CID::SIZE] = { 0 };
	data[0] = value;
	return UserPtr(new User(CID(data)));
}

template<class F> double elapsedMilliseconds(F&& action) {
	const auto start = std::chrono::steady_clock::now();
	action();
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void writeQueueFile(QueueManager& queue, size_t itemCount, bool includeSource) {
	File file(queue.getQueueFile(), File::WRITE, File::CREATE | File::TRUNCATE);
	BufferedOutputStream<false> output(&file);
	const auto sourceCID = makeUser(9)->getCID().toBase32();
	string escapedTarget;
	string base32;

	output.write(SimpleXML::utf8Header);
	output.write(LIT("<Downloads Version=\"" VERSIONSTRING "\">\r\n"));
	for(size_t i = 0; i < itemCount; ++i) {
		const auto target = Util::getPath(Util::PATH_DOWNLOADS) + "benchmark-" + std::to_string(i) + ".bin";
		output.write(LIT("\t<Download Target=\""));
		output.write(SimpleXML::escape(target, escapedTarget, true));
		output.write(LIT("\" Size=\"4096\" Priority=\"3\" Added=\"1\" TTH=\""));
		base32.clear();
		output.write(makeTTH(static_cast<uint32_t>(i + 1)).toBase32(base32));
		if(includeSource) {
			output.write(LIT("\">\r\n\t\t<Source CID=\""));
			output.write(sourceCID);
			output.write(LIT("\"/>\r\n\t</Download>\r\n"));
		} else {
			output.write(LIT("\"/>\r\n"));
		}
	}
	output.write(LIT("</Downloads>\r\n"));
	output.flush();
	file.close();
}

struct QueueBenchmarkResult {
	double loadTime;
	double firstLookupTime;
	double cachedLookupTime;
	size_t loadedItems;
	size_t lookupResults;
};

QueueBenchmarkResult runQueueBenchmark(size_t itemCount, bool includeSource) {
	auto queue = QueueManager::getInstance();
	writeQueueFile(*queue, itemCount, includeSource);
	QueueBenchmarkResult result = { 0, 0, 0, 0, 0 };
	result.loadTime = elapsedMilliseconds([&] { queue->loadQueue([](float) { }); });
	queue->lockedOperation([&](const QueueItem::StringMap& items) { result.loadedItems = items.size(); });
	const auto missingTTH = makeTTH(0);
	result.firstLookupTime = elapsedMilliseconds([&] { result.lookupResults += queue->getTargets(missingTTH).size(); });
	result.cachedLookupTime = elapsedMilliseconds([&] { result.lookupResults += queue->getTargets(missingTTH).size(); });
	return result;
}

void printQueueBenchmark(const QueueBenchmarkResult& result, size_t itemCount, bool includeSource) {
	std::cout << std::fixed << std::setprecision(3)
		<< "\nQueue TTH index benchmark (" << itemCount << " Queue.xml items, "
		<< (includeSource ? "one source each" : "no sources") << ")\n"
		<< "startup queue load: " << result.loadTime << " ms\n"
		<< "first missing-TTH lookup: " << result.firstLookupTime << " ms\n"
		<< "cached missing-TTH lookup: " << result.cachedLookupTime << " ms\n";
}

class QueueTTHIndexTest : public ::testing::Test {
public:
	void SetUp() override {
		const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
		configPath = (std::filesystem::temp_directory_path() / ("dcpp-queue-tth-test-" + std::to_string(ticks))).string() + PATH_SEPARATOR;

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
		SearchManager::newInstance();
		ClientManager::newInstance();
		QueueManager::newInstance();
	}

	void TearDown() override {
		QueueManager::deleteInstance();
		ClientManager::deleteInstance();
		SearchManager::deleteInstance();
		TimerManager::getInstance()->shutdown();
		TimerManager::deleteInstance();
		LogManager::deleteInstance();
		SettingsManager::deleteInstance();
		std::filesystem::remove_all(configPath);
	}

	string target(const string& name) const { return Util::getPath(Util::PATH_DOWNLOADS) + name; }

private:
	string configPath;
};

}

TEST_F(QueueTTHIndexTest, preserves_duplicate_tths_across_add_remove_and_move)
{
	auto queue = QueueManager::getInstance();
	auto user = makeUser(1);
	const HintedUser hintedUser(user, "adc://queue-index.example.invalid");
	const auto tth = makeTTH(1);
	const auto first = target("first.bin");
	const auto second = target("second.bin");
	const auto third = target("third.bin");
	const auto moved = target("moved.bin");

	queue->add(first, 1, tth, hintedUser);
	queue->add(second, 2, tth, hintedUser);
	queue->remove(first);

	auto targets = queue->getTargets(tth);
	ASSERT_EQ(1U, targets.size());
	EXPECT_EQ(second, targets.front());

	queue->add(third, 3, tth, hintedUser);
	targets = queue->getTargets(tth);
	ASSERT_EQ(2U, targets.size());
	EXPECT_NE(targets.end(), std::find(targets.begin(), targets.end(), second));
	EXPECT_NE(targets.end(), std::find(targets.begin(), targets.end(), third));

	queue->move(third, moved);
	targets = queue->getTargets(tth);
	ASSERT_EQ(2U, targets.size());
	EXPECT_NE(targets.end(), std::find(targets.begin(), targets.end(), second));
	EXPECT_NE(targets.end(), std::find(targets.begin(), targets.end(), moved));
	EXPECT_EQ(targets.end(), std::find(targets.begin(), targets.end(), third));

	queue->remove(second);
	queue->remove(moved);
	EXPECT_TRUE(queue->getTargets(tth).empty());
}

TEST_F(QueueTTHIndexTest, tracks_additions_after_an_empty_lookup)
{
	auto queue = QueueManager::getInstance();
	auto user = makeUser(2);
	const HintedUser hintedUser(user, "adc://queue-index.example.invalid");
	const auto tth = makeTTH(2);
	const auto file = target("after-empty-lookup.bin");

	EXPECT_TRUE(queue->getTargets(tth).empty());
	queue->add(file, 1, tth, hintedUser);

	auto targets = queue->getTargets(tth);
	ASSERT_EQ(1U, targets.size());
	EXPECT_EQ(file, targets.front());
	queue->remove(file);
}

TEST_F(QueueTTHIndexTest, keeps_distinct_tths_isolated)
{
	auto queue = QueueManager::getInstance();
	auto user = makeUser(3);
	const HintedUser hintedUser(user, "adc://queue-index.example.invalid");
	const auto firstTTH = makeTTH(3);
	const auto secondTTH = makeTTH(4);
	const auto first = target("distinct-first.bin");
	const auto second = target("distinct-second.bin");

	queue->add(first, 1, firstTTH, hintedUser);
	queue->add(second, 2, secondTTH, hintedUser);

	auto firstTargets = queue->getTargets(firstTTH);
	auto secondTargets = queue->getTargets(secondTTH);
	ASSERT_EQ(1U, firstTargets.size());
	ASSERT_EQ(1U, secondTargets.size());
	EXPECT_EQ(first, firstTargets.front());
	EXPECT_EQ(second, secondTargets.front());

	queue->remove(first);
	queue->remove(second);
}

TEST_F(QueueTTHIndexTest, DISABLED_benchmark_startup_without_sources)
{
#ifdef _DEBUG
	std::cout << "\nQueue TTH benchmark requires a release build.\n";
#else
	const size_t itemCount = 32768;
	const auto result = runQueueBenchmark(itemCount, false);
	EXPECT_EQ(itemCount, result.loadedItems);
	EXPECT_EQ(0U, result.lookupResults);
	printQueueBenchmark(result, itemCount, false);
#endif
}

TEST_F(QueueTTHIndexTest, DISABLED_benchmark_startup_with_one_source)
{
#ifdef _DEBUG
	std::cout << "\nQueue TTH benchmark requires a release build.\n";
#else
	const size_t itemCount = 32768;
	const auto result = runQueueBenchmark(itemCount, true);
	EXPECT_EQ(itemCount, result.loadedItems);
	EXPECT_EQ(0U, result.lookupResults);
	printQueueBenchmark(result, itemCount, true);
#endif
}
