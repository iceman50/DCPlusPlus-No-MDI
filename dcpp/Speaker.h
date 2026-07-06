/*
 * Copyright (C) 2001-2025 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_SPEAKER_H
#define DCPLUSPLUS_DCPP_SPEAKER_H

#include <algorithm>
#include <condition_variable>
#include <unordered_map>
#include <utility>
#include <vector>

#include "CriticalSection.h"

namespace dcpp {

using std::forward;
using std::vector;

template<typename Listener>
class Speaker {
	typedef vector<Listener*> ListenerList;

	struct CallbackFrame {
		CallbackFrame(Speaker* speaker_, Listener* listener_) : speaker(speaker_), listener(listener_), previous(currentFrame()) {
			currentFrame() = this;
		}

		~CallbackFrame() {
			currentFrame() = previous;
		}

		static CallbackFrame*& currentFrame() {
			static thread_local CallbackFrame* frame = nullptr;
			return frame;
		}

		Speaker* speaker;
		Listener* listener;
		CallbackFrame* previous;
	};

	size_t callDepth(Listener* listener) const noexcept {
		size_t depth = 0;
		for(auto frame = CallbackFrame::currentFrame(); frame; frame = frame->previous) {
			if(frame->speaker == this && frame->listener == listener) {
				++depth;
			}
		}
		return depth;
	}

public:
	Speaker() noexcept { }
	virtual ~Speaker() { }

	template<typename... T>
	void fire(T&&... type) noexcept {
		ListenerList snapshot;
		{
			Lock l(listenerCS);
			snapshot = listeners;
		}

		for(auto listener: snapshot) {
			{
				Lock l(listenerCS);
				if(std::find(listeners.begin(), listeners.end(), listener) == listeners.end()) {
					continue;
				}
				++activeCallbacks[listener];
			}

			{
				CallbackFrame frame(this, listener);
				// Named arguments are deliberately passed as lvalues: an event may have
				// multiple listeners and the first one must not consume an rvalue payload.
				listener->on(type...);
			}

			{
				Lock l(listenerCS);
				auto i = activeCallbacks.find(listener);
				if(i != activeCallbacks.end() && --i->second == 0) {
					activeCallbacks.erase(i);
					listenerCV.notify_all();
				}
			}
		}
	}

	void addListener(Listener* aListener) {
		Lock l(listenerCS);
		if(std::find(listeners.begin(), listeners.end(), aListener) == listeners.end())
			listeners.push_back(aListener);
	}

	void removeListener(Listener* aListener) {
		Lock l(listenerCS);
		auto it = std::find(listeners.begin(), listeners.end(), aListener);
		if(it != listeners.end())
			listeners.erase(it);

		const auto localDepth = callDepth(aListener);
		listenerCV.wait(l, [&] {
			auto callback = activeCallbacks.find(aListener);
			return callback == activeCallbacks.end() || callback->second <= localDepth;
		});
	}

	void removeListeners() {
		Lock l(listenerCS);
		listeners.clear();
		listenerCV.wait(l, [&] {
			return std::all_of(activeCallbacks.begin(), activeCallbacks.end(), [this](const auto& callback) {
				return callback.second <= callDepth(callback.first);
			});
		});
	}

protected:
	ListenerList listeners;
	CriticalSection listenerCS;
	std::condition_variable_any listenerCV;
	std::unordered_map<Listener*, size_t> activeCallbacks;
};

} // namespace dcpp

#endif // !defined(SPEAKER_H)
