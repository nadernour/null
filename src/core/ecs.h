#pragma once

#include "core/assert.h"
#include "core/defines.h"

// std
#include <memory>
#include <tuple>
#include <vector>

namespace ne {

// The version changes whenever an id is recycled, so stale handles stay invalid
struct Entity {
  static constexpr uint32_t kInvalidId = ~0U;
  static const Entity Null; // Also the default value

  uint32_t id{kInvalidId};
  uint32_t version{0};

  constexpr bool isValid() const { return id != kInvalidId; }
  constexpr bool operator==(const Entity& iOther) const { return id == iOther.id && version == iOther.version; }
  constexpr bool operator!=(const Entity& iOther) const { return !(*this == iOther); }
};

inline constexpr Entity Entity::Null{};

} // namespace ne

namespace std {
template <>
struct hash<ne::Entity> {
  size_t operator()(const ne::Entity& iEntity) const noexcept {
    return (static_cast<size_t>(iEntity.id) << 32) | static_cast<size_t>(iEntity.version);
  }
};
} // namespace std

namespace ne {

class Registry;

// A component type may define `static void onRemove(Registry&, Entity)`. The registry calls it before the component is
// removed by removeComponent() or destroyEntity(), while the entity and all its components are still intact.
template <typename ComponentType>
concept HasRemoveHook = requires(Registry& ioRegistry, Entity iEntity) { ComponentType::onRemove(ioRegistry, iEntity); };

class IComponentPool {
public:
  virtual ~IComponentPool() = default;
  virtual void invokeRemoveHook(Registry& ioRegistry, Entity iEntity) = 0;
  virtual void removeIfExists(Entity iEntity) = 0;
  virtual bool has(Entity iEntity) const = 0;
  virtual void clear() = 0;
};

// Sparse set: components are stored densely for cache-friendly iteration
template <typename ComponentType>
class ComponentPool : public IComponentPool {
public:
  ComponentPool() = default;
  ComponentPool(size_t iComponentCount, size_t iEntityCount) { reserve(iComponentCount, iEntityCount); }
  ~ComponentPool() override = default;
  ComponentPool(const ComponentPool&) = delete;
  ComponentPool& operator=(const ComponentPool&) = delete;
  ComponentPool(ComponentPool&&) noexcept = default;
  ComponentPool& operator=(ComponentPool&&) noexcept = default;

  void reserve(size_t iComponentCount, size_t iEntityCount = 0) {
    mDenseComponents.reserve(iComponentCount);
    mDenseEntities.reserve(iComponentCount);
    mSparse.reserve(iEntityCount > 0 ? iEntityCount : iComponentCount);
  }

  template <typename... Args>
  ComponentType& emplace(Entity iEntity, Args&&... iArgs) {
    NE_ASSERT(iEntity.isValid(), "Entity handle is invalid.");
    if (iEntity.id >= mSparse.size()) {
      mSparse.resize(iEntity.id + 1, kInvalidIndex);
    }
    NE_ASSERT(mSparse[iEntity.id] == kInvalidIndex, "Entity already has this component.");

    uint32_t denseIdx = static_cast<uint32_t>(mDenseComponents.size());
    mSparse[iEntity.id] = denseIdx;
    mDenseEntities.push_back(iEntity);
    mDenseComponents.emplace_back(std::forward<Args>(iArgs)...);
    return mDenseComponents.back();
  }

  ComponentType& add(Entity iEntity) { return emplace(iEntity); }
  ComponentType& add(Entity iEntity, const ComponentType& iComp) { return emplace(iEntity, iComp); }
  ComponentType& add(Entity iEntity, ComponentType&& iComp) { return emplace(iEntity, std::move(iComp)); }

  ComponentType& getOrAdd(Entity iEntity) {
    if (has(iEntity)) {
      return get(iEntity);
    }
    return add(iEntity);
  }

  // Swap-and-pop: moves the last component into the hole, invalidating references to it
  void remove(Entity iEntity) {
    NE_ASSERT(has(iEntity), "Entity does not have specified component.");
    uint32_t removedDenseIdx = mSparse[iEntity.id];
    uint32_t lastDenseIdx = static_cast<uint32_t>(mDenseComponents.size()) - 1;

    if (removedDenseIdx != lastDenseIdx) {
      Entity lastEntity = mDenseEntities[lastDenseIdx];
      mDenseComponents[removedDenseIdx] = std::move(mDenseComponents[lastDenseIdx]);
      mDenseEntities[removedDenseIdx] = lastEntity;
      mSparse[lastEntity.id] = removedDenseIdx;
    }

    mDenseComponents.pop_back();
    mDenseEntities.pop_back();
    mSparse[iEntity.id] = kInvalidIndex;
  }

  void invokeRemoveHook([[maybe_unused]] Registry& ioRegistry, [[maybe_unused]] Entity iEntity) override {
    if constexpr (HasRemoveHook<ComponentType>) {
      if (has(iEntity)) {
        ComponentType::onRemove(ioRegistry, iEntity);
      }
    }
  }

  void removeIfExists(Entity iEntity) override {
    if (has(iEntity)) {
      remove(iEntity);
    }
  }

  // Compares the full handle, so a stale handle never matches the entity that recycled its id
  bool has(Entity iEntity) const override {
    return iEntity.isValid() && iEntity.id < mSparse.size() && mSparse[iEntity.id] != kInvalidIndex &&
           mDenseEntities[mSparse[iEntity.id]] == iEntity;
  }

  ComponentType& get(Entity iEntity) {
    NE_ASSERT(has(iEntity), "Entity does not have specified component.");
    return mDenseComponents[mSparse[iEntity.id]];
  }

  const ComponentType& get(Entity iEntity) const {
    NE_ASSERT(has(iEntity), "Entity does not have specified component.");
    return mDenseComponents[mSparse[iEntity.id]];
  }

  void clear() override {
    mDenseComponents.clear();
    mDenseEntities.clear();
    mSparse.clear();
  }

  void reset() { clear(); }
  void resetPool() { clear(); }

  size_t size() const { return mDenseComponents.size(); }
  size_t capacity() const { return mDenseComponents.capacity(); }

  ComponentType* data() { return mDenseComponents.data(); }
  const ComponentType* data() const { return mDenseComponents.data(); }

  const std::vector<Entity>& entities() const { return mDenseEntities; }
  const std::vector<ComponentType>& components() const { return mDenseComponents; }

  auto begin() { return mDenseComponents.begin(); }
  auto end() { return mDenseComponents.end(); }
  auto begin() const { return mDenseComponents.cbegin(); }
  auto end() const { return mDenseComponents.cend(); }

private:
  static constexpr uint32_t kInvalidIndex = ~0U;
  std::vector<uint32_t> mSparse;
  std::vector<Entity> mDenseEntities;
  std::vector<ComponentType> mDenseComponents;
};

template <typename... Components>
class View {
public:
  View(ComponentPool<Components>&... iPools) : mPools(std::forward_as_tuple(iPools...)) {}

  // The callback takes (Entity, Components&...) or (Components&...)
  template <typename Func>
  void each(Func&& iFunc) {
    const std::vector<Entity>* smallestEntities = getSmallestEntities();
    if (!smallestEntities)
      return;

    for (Entity entity : *smallestEntities) {
      if ((std::get<ComponentPool<Components>&>(mPools).has(entity) && ...)) {
        if constexpr (std::is_invocable_v<Func, Entity, Components&...>) {
          iFunc(entity, std::get<ComponentPool<Components>&>(mPools).get(entity)...);
        } else {
          iFunc(std::get<ComponentPool<Components>&>(mPools).get(entity)...);
        }
      }
    }
  }

private:
  const std::vector<Entity>* getSmallestEntities() const {
    const std::vector<Entity>* smallest = nullptr;
    size_t minSize = SIZE_MAX;

    std::apply(
        [&](const auto&... iPools) {
          auto checkPool = [&](const auto& iPool) {
            if (iPool.size() < minSize) {
              minSize = iPool.size();
              smallest = &iPool.entities();
            }
          };
          (checkPool(iPools), ...);
        },
        mPools);

    return smallest;
  }

  std::tuple<ComponentPool<Components>&...> mPools;
};

class Registry {
public:
  Registry(int32_t iExpectedEntityCount = 100) { reserveEntities(iExpectedEntityCount); }

  ~Registry() = default;
  Registry(const Registry&) = delete;
  Registry& operator=(const Registry&) = delete;
  Registry(Registry&&) noexcept = default;
  Registry& operator=(Registry&&) noexcept = default;

  void reserveEntities(int32_t iCount) { mGenerations.reserve(iCount); }

  Entity createEntity() {
    uint32_t id;
    if (!mFreeEntities.empty()) {
      id = mFreeEntities.back();
      mFreeEntities.pop_back();
    } else {
      id = static_cast<uint32_t>(mGenerations.size());
      mGenerations.push_back(0);
    }
    return Entity{id, mGenerations[id]};
  }

  void destroyEntity(Entity iEntity) {
    if (!isValid(iEntity))
      return;

    // Hooks may destroy other entities or create pools, so the pools are indexed rather than iterated
    for (size_t i = 0; i < mPools.size(); ++i) {
      if (mPools[i]) {
        mPools[i]->invokeRemoveHook(*this, iEntity);
      }
    }
    NE_ASSERT(isValid(iEntity), "A remove hook destroyed the entity it was called for.");

    for (auto& pool : mPools) {
      if (pool) {
        pool->removeIfExists(iEntity);
      }
    }

    mGenerations[iEntity.id]++;
    mFreeEntities.push_back(iEntity.id);
  }

  bool isValid(Entity iEntity) const {
    return iEntity.isValid() && iEntity.id < mGenerations.size() && mGenerations[iEntity.id] == iEntity.version;
  }

  template <typename ComponentType>
  void reserveComponents(size_t iCount = 0) {
    getPool<ComponentType>().reserve(iCount, mGenerations.capacity());
  }

  template <typename ComponentType, typename... Args>
  ComponentType& addComponent(Entity iEntity, Args&&... iArgs) {
    NE_ASSERT(isValid(iEntity), "Entity is not alive.");
    return getPool<ComponentType>().emplace(iEntity, std::forward<Args>(iArgs)...);
  }

  template <typename ComponentType>
  void removeComponent(Entity iEntity) {
    NE_ASSERT(isValid(iEntity), "Entity is not alive.");
    NE_ASSERT(hasComponent<ComponentType>(iEntity), "Entity does not have specified component.");
    if constexpr (HasRemoveHook<ComponentType>) {
      ComponentType::onRemove(*this, iEntity);
    }
    getPool<ComponentType>().remove(iEntity);
  }

  template <typename ComponentType>
  bool hasComponent(Entity iEntity) const {
    const auto* pool = getPoolIfExists<ComponentType>();
    return pool && pool->has(iEntity);
  }

  template <typename ComponentType>
  ComponentType& getComponent(Entity iEntity) {
    NE_ASSERT(isValid(iEntity), "Entity is not alive.");
    return getPool<ComponentType>().get(iEntity);
  }

  template <typename ComponentType>
  const ComponentType& getComponent(Entity iEntity) const {
    NE_ASSERT(isValid(iEntity), "Entity is not alive.");
    const auto* pool = getPoolIfExists<ComponentType>();
    NE_ASSERT(pool != nullptr, "Component pool does not exist.");
    return pool->get(iEntity);
  }

  template <typename... Components>
  View<Components...> view() {
    return View<Components...>(getPool<Components>()...);
  }

  template <typename ComponentType>
  ComponentPool<ComponentType>& getPool() {
    uint32_t typeId = getComponentTypeId<ComponentType>();
    if (typeId >= mPools.size()) {
      mPools.resize(typeId + 1);
    }
    if (!mPools[typeId]) {
      mPools[typeId] = std::make_unique<ComponentPool<ComponentType>>(0, mGenerations.capacity());
    }
    return *static_cast<ComponentPool<ComponentType>*>(mPools[typeId].get());
  }

  template <typename ComponentType>
  const ComponentPool<ComponentType>* getPoolIfExists() const {
    uint32_t typeId = getComponentTypeId<ComponentType>();
    if (typeId < mPools.size() && mPools[typeId]) {
      return static_cast<const ComponentPool<ComponentType>*>(mPools[typeId].get());
    }
    return nullptr;
  }

  // Destroys every entity without running remove hooks. Versions are bumped rather than cleared, so handles from before
  // the reset stay invalid once their ids are reused.
  void reset() {
    for (auto& pool : mPools) {
      if (pool) {
        pool->clear();
      }
    }
    mFreeEntities.clear();
    for (uint32_t id = 0; id < mGenerations.size(); ++id) {
      mGenerations[id]++;
      mFreeEntities.push_back(id);
    }
  }

  size_t size() const { return mGenerations.size() - mFreeEntities.size(); }
  size_t capacity() const { return mGenerations.capacity(); }

private:
  template <typename T>
  static uint32_t getComponentTypeId() {
    static uint32_t id = sNextComponentId++;
    return id;
  }

  static inline uint32_t sNextComponentId = 0;

  std::vector<uint32_t> mGenerations;
  std::vector<uint32_t> mFreeEntities;
  std::vector<std::unique_ptr<IComponentPool>> mPools;
};

} // namespace ne