#pragma once

#include <functional>
#include <unordered_map>
#include <cstdint>

namespace ne {

using CallbackId = uint64_t;

template <typename... Args>
class Event {
public:
  using Callback = std::function<void(Args...)>;

  CallbackId add(Callback iCallback) {
    CallbackId id = mNextId++;
    mCallbacks[id] = std::move(iCallback);
    return id;
  }

  bool remove(CallbackId iId) { return mCallbacks.erase(iId) > 0; }

  void clear() { mCallbacks.clear(); }

  void broadcast(Args... iArgs) const {
    for (const auto& [id, callback] : mCallbacks) {
      if (callback) {
        callback(iArgs...);
      }
    }
  }

  bool empty() const { return mCallbacks.empty(); }
  size_t size() const { return mCallbacks.size(); }

private:
  CallbackId mNextId = 1;
  std::unordered_map<CallbackId, Callback> mCallbacks;
};

} // namespace ne
