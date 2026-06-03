/*

  DC++ Widget Toolkit

  copyright (c) 2026, iceman50

  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met:

      * Redistributions of source code must retain the above copyright notice,
        this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright notice,
        this list of conditions and the following disclaimer in the documentation
        and/or other materials provided with the distribution.
      * Neither the name of the DWT nor the names of its contributors
        may be used to endorse or promote products derived from this software
        without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef DWT_UTIL_INTRUSIVEPTR_H_
#define DWT_UTIL_INTRUSIVEPTR_H_

#include <cstddef>
#include <type_traits>
#include <utility>

namespace dwt { namespace util {

// Intrusive smart pointer implementation tailored for DWT.

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

} }

#endif /* DWT_UTIL_INTRUSIVEPTR_H_ */
