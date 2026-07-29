#include "testbase.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <dcpp/Speaker.h>

using namespace dcpp;
using namespace std::chrono_literals;

namespace {

class TestSpeakerListener {
public:
	virtual ~TestSpeakerListener() = default;
	template<int I> struct X { };
	using Event = X<0>;

	virtual void on(Event, bool removeSelf) noexcept = 0;
};

class TestSpeaker : public Speaker<TestSpeakerListener> {
public:
	bool contains(TestSpeakerListener* listener) {
		Lock l(listenerCS);
		return std::find(listeners.begin(), listeners.end(), listener) != listeners.end();
	}
};

class ReentrantListener : public TestSpeakerListener {
public:
	explicit ReentrantListener(TestSpeaker& speaker_) : speaker(speaker_) { }

	void on(Event, bool removeSelf) noexcept override {
		{
			std::unique_lock<std::mutex> l(mutex);
			++entered;
			cv.notify_all();
			cv.wait(l, [this] { return entered == 2; });
		}

		if(removeSelf) {
			speaker.removeListener(this);
			{
				std::lock_guard<std::mutex> l(mutex);
				removalCompleted = true;
			}
			cv.notify_all();
		} else {
			// Once the listener disappears, the other callback is inside
			// removeListener and waiting for our active count to decrement.
			while(speaker.contains(this)) {
				std::this_thread::yield();
			}
		}
	}

	TestSpeaker& speaker;
	std::mutex mutex;
	std::condition_variable cv;
	int entered = 0;
	bool removalCompleted = false;
};

class BlockingListener : public TestSpeakerListener {
public:
	void on(Event, bool) noexcept override {
		std::unique_lock<std::mutex> l(mutex);
		entered = true;
		cv.notify_all();
		cv.wait(l, [this] { return release; });
	}

	std::mutex mutex;
	std::condition_variable cv;
	bool entered = false;
	bool release = false;
	bool removalCompleted = false;
};

} // namespace

TEST(testspeaker, reentrant_removal_wakes_at_local_callback_depth) {
	auto speaker = new TestSpeaker;
	auto listener = new ReentrantListener(*speaker);
	speaker->addListener(listener);

	std::thread first([&] { speaker->fire(TestSpeakerListener::Event(), true); });
	std::thread second([&] { speaker->fire(TestSpeakerListener::Event(), false); });

	bool completed;
	{
		std::unique_lock<std::mutex> l(listener->mutex);
		completed = listener->cv.wait_for(l, 2s, [&] { return listener->removalCompleted; });
	}

	second.join();
	if(!completed) {
		// Keep the synchronization objects alive so a failing implementation
		// can be reported without hanging the entire test executable.
		first.detach();
		ADD_FAILURE() << "Re-entrant listener removal did not wake at its local callback depth";
		return;
	}

	first.join();
	delete listener;
	delete speaker;
}

TEST(testspeaker, external_removal_waits_for_active_callback) {
	auto speaker = new TestSpeaker;
	auto listener = new BlockingListener;
	speaker->addListener(listener);

	std::thread firing([&] { speaker->fire(TestSpeakerListener::Event(), false); });
	bool entered;
	{
		std::unique_lock<std::mutex> l(listener->mutex);
		entered = listener->cv.wait_for(l, 2s, [&] { return listener->entered; });
	}
	if(!entered) {
		firing.detach();
		ADD_FAILURE() << "Listener callback did not start";
		return;
	}

	std::thread removing([&] {
		speaker->removeListener(listener);
		{
			std::lock_guard<std::mutex> l(listener->mutex);
			listener->removalCompleted = true;
		}
		listener->cv.notify_all();
	});

	{
		std::unique_lock<std::mutex> l(listener->mutex);
		EXPECT_FALSE(listener->cv.wait_for(l, 100ms, [&] { return listener->removalCompleted; }));
		listener->release = true;
	}
	listener->cv.notify_all();

	bool completed;
	{
		std::unique_lock<std::mutex> l(listener->mutex);
		completed = listener->cv.wait_for(l, 2s, [&] { return listener->removalCompleted; });
	}
	firing.join();
	if(!completed) {
		removing.detach();
		ADD_FAILURE() << "External listener removal did not finish after its callback returned";
		return;
	}

	removing.join();
	delete listener;
	delete speaker;
}
