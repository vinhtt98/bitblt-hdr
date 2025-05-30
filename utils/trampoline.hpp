#pragma once
template <typename T>
class trampoline
{
public:
	trampoline() { object_ = nullptr; }
	T* get() const { return object_; }
	operator T* () const { return this->get(); }
	void** operator&() { return reinterpret_cast<void**>(&object_); }

private:
	T* object_;
};
