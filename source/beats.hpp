#pragma once

#include <memory>

class SharedObject;
using SharedObjectPtr = std::shared_ptr<SharedObject>;

class SharedObject : public std::enable_shared_from_this<SharedObject> {
public:
	virtual ~SharedObject() = default;

	SharedObject &operator=(const SharedObject &) = delete;

	SharedObjectPtr asSharedObject() {
		return shared_from_this();
	}

	template <typename T>
	std::shared_ptr<T> static_self_cast() {
		return std::static_pointer_cast<T>(shared_from_this());
	}

	template <typename T>
	std::shared_ptr<T> dynamic_self_cast() {
		return std::dynamic_pointer_cast<T>(shared_from_this());
	}

	template <typename TargetType, typename SourceType>
	std::shared_ptr<TargetType> static_self_cast(std::shared_ptr<SourceType> source) {
		return std::static_pointer_cast<TargetType>(source);
	}

	template <typename TargetType, typename SourceType>
	std::shared_ptr<TargetType> dynamic_self_cast(std::shared_ptr<SourceType> source) {
		return std::dynamic_pointer_cast<TargetType>(source);
	}
};
