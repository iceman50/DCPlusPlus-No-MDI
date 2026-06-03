/*
 * Copyright (C) 2001-2026 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_POINTER_H
#define DCPLUSPLUS_DCPP_POINTER_H

#include <atomic>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace dcpp {

using std::unique_ptr;
using std::forward;

//Copied from LibDWT which handles the same problem

template<class T>
class intrusive_ptr {
public:
	typedef T element_type;

	intrusive_ptr() noexcept : px(nullptr) { }

	intrusive_ptr(T* p, bool add_ref = true) : px(p) {
		if(px && add_ref) {
			intrusive_ptr_add_ref(px);
		}
	}

	intrusive_ptr(const intrusive_ptr& rhs) : px(rhs.px) {
		if(px) {
			intrusive_ptr_add_ref(px);
		}
	}

	template<class U, typename std::enable_if<std::is_convertible<U*, T*>::value, int>::type = 0>
	intrusive_ptr(const intrusive_ptr<U>& rhs) : px(rhs.get()) {
		if(px) {
			intrusive_ptr_add_ref(px);
		}
	}

	template<class U, typename std::enable_if<std::is_convertible<U*, T*>::value, int>::type = 0>
	intrusive_ptr(intrusive_ptr<U>&& rhs) noexcept : px(rhs.px) {
		rhs.px = nullptr;
	}

	intrusive_ptr(intrusive_ptr&& rhs) noexcept : px(rhs.px) {
		rhs.px = nullptr;
	}

	~intrusive_ptr() {
		if(px) {
			intrusive_ptr_release(px);
		}
	}

	intrusive_ptr& operator=(const intrusive_ptr& rhs) {
		intrusive_ptr(rhs).swap(*this);
		return *this;
	}

	template<class U, typename std::enable_if<std::is_convertible<U*, T*>::value, int>::type = 0>
	intrusive_ptr& operator=(const intrusive_ptr<U>& rhs) {
		intrusive_ptr(rhs).swap(*this);
		return *this;
	}

	template<class U, typename std::enable_if<std::is_convertible<U*, T*>::value, int>::type = 0>
	intrusive_ptr& operator=(intrusive_ptr<U>&& rhs) noexcept {
		intrusive_ptr(static_cast<intrusive_ptr<U>&&>(rhs)).swap(*this);
		return *this;
	}

	intrusive_ptr& operator=(intrusive_ptr&& rhs) noexcept {
		intrusive_ptr(static_cast<intrusive_ptr&&>(rhs)).swap(*this);
		return *this;
	}

	intrusive_ptr& operator=(T* rhs) {
		intrusive_ptr(rhs).swap(*this);
		return *this;
	}

	void reset() noexcept {
		intrusive_ptr().swap(*this);
	}

	void reset(T* rhs, bool add_ref = true) {
		intrusive_ptr(rhs, add_ref).swap(*this);
	}

	T* get() const noexcept {
		return px;
	}

	T& operator*() const noexcept {
		return *px;
	}

	T* operator->() const noexcept {
		return px;
	}

	explicit operator bool() const noexcept {
		return px != nullptr;
	}

	bool operator!() const noexcept {
		return px == nullptr;
	}

	void swap(intrusive_ptr& rhs) noexcept {
		std::swap(px, rhs.px);
	}

private:
	template<class U> friend class intrusive_ptr;

	T* px;
};

template<class T, class U>
inline bool operator==(const intrusive_ptr<T>& a, const intrusive_ptr<U>& b) noexcept {
	return a.get() == b.get();
}

template<class T, class U>
inline bool operator!=(const intrusive_ptr<T>& a, const intrusive_ptr<U>& b) noexcept {
	return a.get() != b.get();
}

template<class T, class U>
inline bool operator<(const intrusive_ptr<T>& a, const intrusive_ptr<U>& b) noexcept {
	return a.get() < b.get();
}

template<class T>
inline bool operator==(const intrusive_ptr<T>& a, std::nullptr_t) noexcept {
	return !a;
}

template<class T>
inline bool operator==(std::nullptr_t, const intrusive_ptr<T>& b) noexcept {
	return !b;
}

template<class T>
inline bool operator!=(const intrusive_ptr<T>& a, std::nullptr_t) noexcept {
	return static_cast<bool>(a);
}

template<class T>
inline bool operator!=(std::nullptr_t, const intrusive_ptr<T>& b) noexcept {
	return static_cast<bool>(b);
}

template<class T>
inline void swap(intrusive_ptr<T>& a, intrusive_ptr<T>& b) noexcept {
	a.swap(b);
}

template<class T>
inline T* get_pointer(const intrusive_ptr<T>& p) noexcept {
	return p.get();
}

template<class T, class U>
inline intrusive_ptr<T> static_pointer_cast(const intrusive_ptr<U>& p) {
	return intrusive_ptr<T>(static_cast<T*>(p.get()));
}

template<class T, class U>
inline intrusive_ptr<T> const_pointer_cast(const intrusive_ptr<U>& p) {
	return intrusive_ptr<T>(const_cast<T*>(p.get()));
}

template<class T, class U>
inline intrusive_ptr<T> dynamic_pointer_cast(const intrusive_ptr<U>& p) {
	return intrusive_ptr<T>(dynamic_cast<T*>(p.get()));
}

template<typename T>
class intrusive_ptr_base
{
public:
	bool unique() noexcept {
		return (ref == 1);
	}

protected:
	intrusive_ptr_base() noexcept : ref(0) { }

private:
	friend void intrusive_ptr_add_ref(intrusive_ptr_base* p) { ++p->ref; }
	friend void intrusive_ptr_release(intrusive_ptr_base* p) { if(--p->ref == 0) { delete static_cast<T*>(p); } }

	std::atomic_long ref;
};

struct DeleteFunction {
	template<typename T>
	void operator()(const T& p) const { delete p; }
};

} // namespace dcpp

#endif // !defined(POINTER_H)
