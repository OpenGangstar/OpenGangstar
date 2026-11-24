#pragma once

#include "retail_actor_scene.h"
#include "retail_animation.h"

class RetailTimeline;

// Original plain animator, manually evaluated by Character after timeline detach. Nodes
// retain this animator; the animator keeps only a weak scene owner. Create via
// create(), then bind a genuine shared scene. There is no ownership cycle.
class RetailActorPose final : public RetailActorSceneAnimator,
                             public RetailAnimationPoseEvaluator,
                             public std::enable_shared_from_this<RetailActorPose> {
public:
    static std::shared_ptr<RetailActorPose> create(std::shared_ptr<const CityAnimationLibrary>);
    RetailActorPose(const RetailActorPose&) = delete;
    RetailActorPose& operator=(const RetailActorPose&) = delete;
    void bind(std::shared_ptr<RetailActorScene>);
    void forceBind();
    void onBind(RetailActorSceneNode&) override;
    void onUnbind(RetailActorSceneNode&) noexcept override;
    void animateNode(RetailActorSceneNode&, uint32_t) override;
    void apply(uint32_t absoluteMs);
    std::array<float, 3> evaluate(int32_t relativeMs, int32_t absoluteMs) override;
    uint32_t periodMs() const { return period; }
    size_t bindingCount() const { return bindings.size(); }
    const RetailActorSceneNode* target(size_t index) const { return bindings.at(index).node; }
    int32_t keyIndex(size_t index) const { return bindings.at(index).keyIndex; }
    const RetailActorSceneNode* boundRoot() const { return root; }
    bool boundTo(const RetailActorScene&) const;
    // The actual factory attachment. Character retains the first object then
    // clears this sole field before binding. apply/animateNode do not read it.
    const std::shared_ptr<RetailTimeline>& timeline() const { return attachedTimeline; }
    void setTimeline(std::shared_ptr<RetailTimeline>);
    void setMode(int32_t value) { mode = value; }
    void setWeight(float value) { weight = value; }
    int32_t getMode() const { return mode; }
    float getWeight() const { return weight; }

private:
    explicit RetailActorPose(std::shared_ptr<const CityAnimationLibrary>);
    struct Binding {
        RetailActorSceneNode* node = nullptr;
        std::array<float, 3>* vector = nullptr;
        std::array<float, 4>* quaternion = nullptr;
        int32_t keyIndex = 0;
    };
    std::shared_ptr<const CityAnimationLibrary> library;
    std::shared_ptr<RetailTimeline> attachedTimeline;
    std::weak_ptr<RetailActorScene> owner;
    RetailActorSceneNode* root = nullptr;
    RetailActorSceneNode* rootMotion = nullptr;
    std::vector<Binding> bindings;
    uint32_t period = 0;
    int32_t mode = 0;
    float weight = 1;
};
