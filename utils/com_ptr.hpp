#pragma once
#include <memory>
#include <utility>
#include <concepts>
#include <unknwnbase.h>

#ifndef _DEBUG
#define printf(...) ((void) 0)
#endif

template <class T>
concept is_com_obj = std::is_base_of<IUnknown, T>::value;

template <typename T> requires is_com_obj<T>
class com_ptr
{
public:
	com_ptr(T* ptr = nullptr) : ptr_(ptr) {}

	com_ptr(const com_ptr<T>& other)
	{
		ptr_ = other.ptr_;

		if (ptr_)
		{
			const auto ref = ptr_->AddRef();
			printf("add ref %p " __FUNCSIG__ ", ref = %u\n", ptr_, ref);
		}
	}

	com_ptr(com_ptr<T>&& other)
	{
		ptr_ = other.ptr_;
		other.ptr_ = nullptr;
	}

	~com_ptr()
	{
		if (ptr_)
		{
			const auto ref = ptr_->Release();
			printf("release %p " __FUNCSIG__ ", ref = %u\n", ptr_, ref);
		}
	}

	T* get() const
	{
		return ptr_;
	}

	bool valid() const
	{
		return ptr_;
	}

	operator bool() { return this->valid(); }
	operator T* () const { return this->get(); }

	T* operator->() const
	{
		return this->get();
	}

	operator T* const* ()
	{
		return &ptr_;
	}

	operator T** ()
	{
		if (ptr_)
		{
			const auto ref = ptr_->Release();
			printf("release %p " __FUNCSIG__ ", ref = %u\n", ptr_, ref);
			ptr_ = nullptr;
		}

		return &ptr_;
	}

	operator void** ()
	{
		return reinterpret_cast<void**>(static_cast<T**>(*this));
	}

	com_ptr<T>& operator=(T* ptr)
	{
		if (ptr == ptr_)
			return *this;

		if (ptr_)
		{
			const auto ref = ptr_->Release();
			printf("release %p " __FUNCSIG__ ", ref = %u\n", ptr_, ref);
		}

		ptr_ = ptr;
		return *this;
	}

	com_ptr<T>& operator=(const com_ptr<T>& other)
	{
		if (this != &other)
		{
			if (ptr_) ptr_->Release();
			ptr_ = other.ptr_;
			if (ptr_) ptr_->AddRef();
		}

		return *this;
	}

	com_ptr<T>& operator=(com_ptr<T>&& other)
	{
		if (this != &other)
		{
			if (ptr_) ptr_->Release();
			ptr_ = other.ptr_;
			other.ptr_ = nullptr;
		}

		return *this;
	}

	template<typename Ty>
	com_ptr<Ty> as()
	{
		com_ptr<Ty> result;

		if (ptr_)
			ptr_->QueryInterface(__uuidof(Ty), result);

		return result;
	}

private:
	T* ptr_;
};
