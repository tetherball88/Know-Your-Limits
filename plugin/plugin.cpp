#include <fmt/format.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "PCH.h"
#include "RE/N/NiAVObject.h"
#include "RE/R/ReferenceArray.h"

namespace {
    // Task interface pointer is obtained on demand using SKSE::GetTaskInterface();
    void SetupLogging() {
        auto logDir = SKSE::log::log_directory();
        if (!logDir) {
            if (auto* console = RE::ConsoleLog::GetSingleton()) {
                console->Print("Know Your Limits: log directory unavailable");
            }
            return;
        }

        std::filesystem::path logPath = *logDir;
        if (!std::filesystem::is_directory(logPath)) {
            logPath = logPath.parent_path();
        }
        logPath /= "KnowYourLimits.log";

        std::error_code ec;
        std::filesystem::create_directories(logPath.parent_path(), ec);
        if (ec) {
            if (auto* console = RE::ConsoleLog::GetSingleton()) {
                console->Print("Know Your Limits: failed to create log folder (%s)", ec.message().c_str());
            }
            return;
        }

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
        auto logger = std::make_shared<spdlog::logger>("KnowYourLimits", std::move(sink));
        logger->set_level(spdlog::level::debug);
        logger->flush_on(spdlog::level::info);
        logger->set_pattern("[%H:%M:%S] [%l] %v");

        spdlog::set_default_logger(std::move(logger));
        spdlog::info("Logging to {}", logPath.string());
    }

    std::string GetActorName(RE::Actor* actor) {
        if (!actor) {
            return "<none>";
        }

        if (const char* name = actor->GetDisplayFullName(); name && *name != '\0') {
            return name;
        }

        if (const char* baseName = actor->GetName(); baseName && *baseName != '\0') {
            return baseName;
        }

        return "<unnamed>";
    }

    std::string_view GetNodeLabel(const RE::BSFixedString& nodeName) {
        const char* data = nodeName.data();
        return data && *data != '\0' ? std::string_view{data} : std::string_view{"<empty>"};
    }

    std::string JoinNodeLabels(const std::vector<RE::BSFixedString>& nodeNames) {
        std::string result;
        for (std::size_t i = 0; i < nodeNames.size(); ++i) {
            if (i > 0) {
                result.append(", ");
            }
            const auto label = GetNodeLabel(nodeNames[i]);
            result.append(label.data(), label.size());
        }
        return result;
    }

    void PrintToConsole(std::string_view message) {
        SKSE::log::info("{}", message);
        if (auto* console = RE::ConsoleLog::GetSingleton()) {
            console->Print("%s", message.data());
        }
    }

    namespace Monitoring {
        struct MonitorEntry {
            std::uint32_t probeHandle{0};
            std::uint32_t targetHandle{0};
            std::vector<RE::BSFixedString> probeNodes;
            RE::BSFixedString targetNode;
            // monitors are indefinite (stopped via StopBoneMonitor)
            float distanceThreshold{0.0f};
            float restoreThreshold{0.0f};
            std::vector<bool> movedFlags;
            bool waitingForBones{false};
            // Cached bone pointers to avoid per-tick lookups
            RE::NiPointer<RE::NiAVObject> cachedBaseNode;
            RE::NiPointer<RE::NiAVObject> cachedTipNode;
            std::vector<RE::NiPointer<RE::NiAVObject>> cachedMiddleBones;
            // Track maximum penetration depth reached
            float maxPenetration{0.0f};
            // Track maximum penetration beyond threshold to minimize repeated bone updates
            float maxPenetrationBeyondThreshold{0.0f};
        };

        std::mutex s_monitorMutex;
        std::vector<MonitorEntry> s_monitors;

        std::mutex s_uiTickMutex;
        std::atomic<bool> s_uiTickActive{false};
        std::chrono::steady_clock::time_point s_lastTickTime{};

        // Configurable tick interval - can be set from Papyrus, defaults to 50ms
        std::atomic<int> s_tickIntervalMs{50};

        // Shutdown synchronization for background thread
        std::atomic<bool> s_shutdownRequested{false};
        std::condition_variable s_shutdownCV;
        std::mutex s_shutdownMutex;

        // Run the monitor tick at most 4 times per second to avoid UI thread churn.
        constexpr float kPositionTolerance = 0.1f;  // Tolerance for position comparisons
        constexpr float kMaxBoneOffset = 1.3f;      // Maximum bone offset to prevent runaway feedback

        void ProcessTick();

        void SetTickInterval(int intervalMs) {
            // Clamp interval to reasonable bounds (16ms to 1000ms)
            const int clampedInterval = std::clamp(intervalMs, 16, 1000);
            s_tickIntervalMs.store(clampedInterval, std::memory_order_relaxed);
            SKSE::log::info("Tick interval set to {}ms", clampedInterval);
        }

        int GetTickInterval() {
            return s_tickIntervalMs.load(std::memory_order_relaxed);
        }

        void QueueTick() {
            // Use atomic for quick check without lock
            if (s_uiTickActive.load(std::memory_order_acquire)) {
                return;
            }

            std::lock_guard<std::mutex> lk(s_uiTickMutex);
            // Double-check after acquiring lock
            if (s_uiTickActive.load(std::memory_order_relaxed)) {
                return;
            }

            if (auto* task = SKSE::GetTaskInterface()) {
                s_uiTickActive.store(true, std::memory_order_release);
                s_lastTickTime = std::chrono::steady_clock::now();
                task->AddUITask([]() { ProcessTick(); });
            } else {
                SKSE::log::critical("Task interface unavailable; cannot start monitor updates.");
            }
        }

        void ScheduleNextTick() {
            // Check for shutdown before scheduling
            if (s_shutdownRequested.load(std::memory_order_acquire)) {
                s_uiTickActive.store(false, std::memory_order_release);
                return;
            }

            auto* task = SKSE::GetTaskInterface();
            if (!task) {
                s_uiTickActive.store(false, std::memory_order_release);
                SKSE::log::critical("Task interface unavailable; stopping monitor updates.");
                return;
            }

            // Use a detached thread for the sleep, but with shutdown awareness
            // This thread is short-lived (just sleeps then queues) so detach is acceptable
            std::thread([task]() {
                const auto intervalMs = s_tickIntervalMs.load(std::memory_order_relaxed);
                const auto interval = std::chrono::milliseconds{intervalMs};

                // Use condition variable for interruptible sleep
                {
                    std::unique_lock<std::mutex> lk(s_shutdownMutex);
                    if (s_shutdownCV.wait_for(lk, interval, []() {
                            return s_shutdownRequested.load(std::memory_order_acquire);
                        })) {
                        // Shutdown was requested during sleep
                        s_uiTickActive.store(false, std::memory_order_release);
                        return;
                    }
                }

                // Check shutdown again after waking
                if (s_shutdownRequested.load(std::memory_order_acquire)) {
                    s_uiTickActive.store(false, std::memory_order_release);
                    return;
                }

                // Queue the actual processing on UI thread
                task->AddUITask([]() {
                    s_lastTickTime = std::chrono::steady_clock::now();
                    ProcessTick();
                });
            }).detach();
        }

        void StopAllMonitoring() {
            // Signal shutdown to any sleeping threads
            s_shutdownRequested.store(true, std::memory_order_release);
            s_shutdownCV.notify_all();

            s_uiTickActive.store(false, std::memory_order_release);
            SKSE::log::info("Monitoring system stopped.");
        }

        void ResetShutdownState() {
            // Called when starting fresh monitoring after a shutdown
            s_shutdownRequested.store(false, std::memory_order_release);
        }

        void UpdateNodeWorldData(RE::NiAVObject* node) {
            if (!node) {
                return;
            }

            RE::NiUpdateData updateData;
            node->UpdateWorldData(&updateData);
        }

        void RestoreBonePosition(RE::Actor* actor, const RE::BSFixedString& nodeName) {
            if (!actor) {
                return;
            }

            auto* node = actor->GetNodeByName(nodeName);

            if (!node) {
                SKSE::log::warn("RestoreBone: bone {} not found on actor {}", nodeName, GetActorName(actor));
                return;
            }

            SKSE::log::info("RestoreBone: {} restoring to originalY={:.3f}", nodeName.c_str(), 0.0f);

            node->local.translate = RE::NiPoint3{0.0f, 0.0f, 0.0f};

            UpdateNodeWorldData(node);
        }

        RE::NiPointer<RE::Actor> LookupActorByHandle(std::uint32_t handle) {
            RE::NiPointer<RE::Actor> actor;
            RE::Actor::LookupByHandle(static_cast<RE::RefHandle>(handle), actor);
            return actor;
        }

        void RestoreMiddleBonesForEntry(const Monitoring::MonitorEntry& entry) {
            RE::NiPointer<RE::Actor> probeActor = LookupActorByHandle(entry.probeHandle);
            if (!probeActor) {
                return;
            }

            for (std::size_t idx = 1; idx < entry.probeNodes.size() - 1; ++idx) {
                if (entry.movedFlags[idx]) {
                    RestoreBonePosition(probeActor.get(), entry.probeNodes[idx]);
                    SKSE::log::info("Restored bone {} for actor {}", GetNodeLabel(entry.probeNodes[idx]),
                                    GetActorName(probeActor.get()));
                }
            }
        }

        void MoveBoneToTarget(RE::Actor* actor, const RE::BSFixedString& nodeName, float penetrationDepth) {
            if (!actor) {
                return;
            }

            auto* node = actor->GetNodeByName(nodeName);

            if (!node) {
                SKSE::log::warn("MoveBone: bone {} not found on actor {}", nodeName, GetActorName(actor));
                return;
            }
            // Calculate how much to move the bone backwards along Y axis
            // penetrationDepth is how far beyond threshold the bone has gone
            float yOffset = -penetrationDepth;

            // Apply offset to ORIGINAL position, not current position
            RE::NiPoint3 newPos = RE::NiPoint3{0.0f, 0.0f, 0.0f};
            newPos.y += yOffset;

            // Check if bone is already at target position (within tolerance)
            const float currentY = node->local.translate.y;
            const float deltaY = std::abs(currentY - newPos.y);

            if (deltaY < kPositionTolerance) {
                // Already at target position, no need to update
                return;
            }

            SKSE::log::info("MoveBone: {} originalY={:.3f} offset={:.3f} newY={:.3f}", nodeName.c_str(), 0.0f, yOffset,
                            newPos.y);

            node->local.translate = newPos;

            UpdateNodeWorldData(node);
        }

        bool AddMonitor(RE::Actor* probeActor, const std::vector<RE::BSFixedString>& probeNodeNames,
                        RE::Actor* targetActor, const RE::BSFixedString& targetNodeName, float distanceThreshold,
                        float restoreThreshold) {
            if (!probeActor || !targetActor) {
                SKSE::log::warn("AddMonitor rejected null actors (probe={}, target={})",
                                static_cast<const void*>(probeActor), static_cast<const void*>(targetActor));
                return false;
            }

            if (probeNodeNames.empty()) {
                SKSE::log::warn("AddMonitor rejected empty probe node list.");
                return false;
            }

            // Monitors created by AddMonitor run indefinitely until stopped.
            // Don't clamp thresholds - allow negative values for pre-emptive scaling
            const auto probeHandle = probeActor->GetHandle().native_handle();
            const auto targetHandle = targetActor->GetHandle().native_handle();

            if (probeHandle == 0 || targetHandle == 0) {
                SKSE::log::warn("AddMonitor received actor with invalid handle (probe={}, target={})", probeHandle,
                                targetHandle);
                return false;
            }

            bool updated = false;
            {
                std::lock_guard<std::mutex> lock(s_monitorMutex);
                for (auto& entry : s_monitors) {
                    if (entry.probeHandle == probeHandle && entry.targetHandle == targetHandle &&
                        entry.targetNode == targetNodeName) {
                        entry.probeNodes = probeNodeNames;
                        entry.distanceThreshold = distanceThreshold;
                        entry.restoreThreshold = restoreThreshold;
                        entry.movedFlags.resize(entry.probeNodes.size(), false);
                        entry.waitingForBones = false;
                        entry.cachedBaseNode.reset();
                        entry.cachedTipNode.reset();
                        entry.cachedMiddleBones.clear();
                        entry.maxPenetration = 0.0f;
                        entry.maxPenetrationBeyondThreshold = 0.0f;
                        updated = true;
                        break;
                    }
                }

                if (!updated) {
                    MonitorEntry newEntry{};
                    newEntry.probeHandle = probeHandle;
                    newEntry.targetHandle = targetHandle;
                    newEntry.probeNodes = probeNodeNames;
                    newEntry.targetNode = targetNodeName;
                    newEntry.distanceThreshold = distanceThreshold;
                    newEntry.restoreThreshold = restoreThreshold;
                    newEntry.movedFlags.resize(probeNodeNames.size(), false);
                    newEntry.waitingForBones = false;
                    newEntry.cachedMiddleBones.resize(probeNodeNames.size());
                    newEntry.maxPenetration = 0.0f;
                    newEntry.maxPenetrationBeyondThreshold = 0.0f;
                    s_monitors.push_back(std::move(newEntry));
                }
            }

            SKSE::log::info(
                "{} bone monitor for {}.[{}] -> {}.{} (shrink threshold {:.2f}, restore threshold {:.2f}, lifetime "
                "indefinite)",
                updated ? "Updated" : "Created", GetActorName(probeActor), JoinNodeLabels(probeNodeNames),
                GetActorName(targetActor), GetNodeLabel(targetNodeName), distanceThreshold, restoreThreshold);

            // Reset shutdown state in case we're starting fresh after a previous shutdown
            ResetShutdownState();
            QueueTick();
            return true;
        }

        std::size_t RemoveMonitors(const std::vector<std::uint32_t>& handles) {
            std::size_t removed = 0;
            bool emptyAfter = false;

            {
                std::lock_guard<std::mutex> lock(s_monitorMutex);
                if (handles.empty()) {
                    // Restore all bones before clearing all monitors
                    for (auto& entry : s_monitors) {
                        RestoreMiddleBonesForEntry(entry);
                    }
                    removed = s_monitors.size();
                    s_monitors.clear();
                } else {
                    // Use a set for O(1) lookup instead of O(n) linear search
                    const std::set<std::uint32_t> handleSet(handles.begin(), handles.end());

                    auto shouldRemove = [&handleSet](const MonitorEntry& entry) {
                        return handleSet.contains(entry.probeHandle) || handleSet.contains(entry.targetHandle);
                    };

                    const auto before = s_monitors.size();
                    // Restore bones for monitors being removed
                    for (auto& entry : s_monitors) {
                        if (shouldRemove(entry)) {
                            RestoreMiddleBonesForEntry(entry);
                        }
                    }
                    std::erase_if(s_monitors, shouldRemove);
                    removed = before - s_monitors.size();
                }

                emptyAfter = s_monitors.empty();
            }

            if (emptyAfter) {
                StopAllMonitoring();
            }

            return removed;
        }

        void Shutdown() {
            SKSE::log::info("Shutting down monitoring system...");

            // Stop all monitoring
            StopAllMonitoring();

            // Restore all bones and clear monitors
            {
                std::lock_guard<std::mutex> lock(s_monitorMutex);
                const auto count = s_monitors.size();

                // Restore all moved bones to their original positions
                for (auto& entry : s_monitors) {
                    RestoreMiddleBonesForEntry(entry);
                }

                s_monitors.clear();
                if (count > 0) {
                    SKSE::log::info("Cleared {} monitor(s)", count);
                }
            }

            SKSE::log::info("Monitoring system shutdown complete.");
        }

        void ProcessTick() {
            // Check for shutdown request
            if (s_shutdownRequested.load(std::memory_order_acquire)) {
                s_uiTickActive.store(false, std::memory_order_release);
                return;
            }

            // Work with monitors directly instead of copying to avoid corrupting NiPoint3 data
            std::lock_guard<std::mutex> lock(s_monitorMutex);

            if (s_monitors.empty()) {
                // Use atomic instead of mutex to avoid potential deadlock
                s_uiTickActive.store(false, std::memory_order_release);
                return;
            }

            // Track which monitors to remove
            std::vector<std::size_t> monitorsToRemove;

            for (std::size_t monitorIdx = 0; monitorIdx < s_monitors.size(); ++monitorIdx) {
                auto& entry = s_monitors[monitorIdx];

                if (entry.probeNodes.empty()) {
                    SKSE::log::warn("Removing monitor with no probe nodes (probeHandle={:#x} targetHandle={:#x})",
                                    entry.probeHandle, entry.targetHandle);
                    monitorsToRemove.push_back(monitorIdx);
                    continue;
                }

                RE::NiPointer<RE::Actor> probeActor = LookupActorByHandle(entry.probeHandle);
                const bool probeValid = static_cast<bool>(probeActor);

                RE::NiPointer<RE::Actor> targetActor = LookupActorByHandle(entry.targetHandle);
                const bool targetValid = static_cast<bool>(targetActor);

                if (!probeValid || !targetValid) {
                    SKSE::log::info("Removing monitor (missing actor) probeHandle={:#x} targetHandle={:#x}",
                                    entry.probeHandle, entry.targetHandle);
                    monitorsToRemove.push_back(monitorIdx);
                    continue;
                }

                // monitors are indefinite; expiration check removed

                entry.movedFlags.resize(entry.probeNodes.size(), false);
                entry.cachedMiddleBones.resize(entry.probeNodes.size());

                auto* targetNode = targetActor->GetNodeByName(entry.targetNode);

                // Get base (first) and tip (last) bones for direction/distance calculation
                if (!entry.cachedBaseNode) {
                    entry.cachedBaseNode =
                        RE::NiPointer<RE::NiAVObject>(probeActor->GetNodeByName(entry.probeNodes[0]));
                }
                if (!entry.cachedTipNode) {
                    entry.cachedTipNode = RE::NiPointer<RE::NiAVObject>(
                        probeActor->GetNodeByName(entry.probeNodes[entry.probeNodes.size() - 1]));
                }
                auto* baseNode = entry.cachedBaseNode.get();
                auto* tipNode = entry.cachedTipNode.get();

                // Get middle bones that will actually be moved
                const std::size_t middleCount = entry.probeNodes.size() > 2 ? entry.probeNodes.size() - 2 : 0;
                std::vector<RE::NiAVObject*> middleBones;
                std::vector<std::size_t> middleBoneIndices;
                middleBones.reserve(middleCount);
                middleBoneIndices.reserve(middleCount);
                for (std::size_t idx = 1; idx < entry.probeNodes.size() - 1; ++idx) {
                    auto& cachedBone = entry.cachedMiddleBones[idx];
                    if (!cachedBone) {
                        cachedBone = RE::NiPointer<RE::NiAVObject>(probeActor->GetNodeByName(entry.probeNodes[idx]));
                    }
                    auto* bone = cachedBone.get();
                    if (bone) {
                        middleBones.push_back(bone);
                        middleBoneIndices.push_back(idx);
                    } else {
                        cachedBone.reset();
                    }
                }

                if (!targetNode || !baseNode || !tipNode || middleBones.empty()) {
                    if (!entry.waitingForBones) {
                        entry.waitingForBones = true;
                        SKSE::log::info(
                            "Waiting for bones (probeHandle={:#x} targetHandle={:#x} target={} base={} tip={} "
                            "middle={})",
                            entry.probeHandle, entry.targetHandle, targetNode ? "ok" : "missing",
                            baseNode ? "ok" : "missing", tipNode ? "ok" : "missing", middleBones.size());
                    }
                    continue;
                }

                if (entry.waitingForBones) {
                    entry.waitingForBones = false;
                    SKSE::log::info("Bones recovered (probeHandle={:#x} targetHandle={:#x})", entry.probeHandle,
                                    entry.targetHandle);
                }

                // Calculate probe chain direction vector (base -> tip) using CURRENT positions
                // Use original local positions only for restoring; penetration should reflect live pose
                const auto& targetPos = targetNode->world.translate;
                const auto& baseWorld = baseNode->world.translate;
                const auto& tipWorld = tipNode->world.translate;

                // Calculate direction vector (normalized) from current positions
                RE::NiPoint3 probeDirection = tipWorld - baseWorld;
                const float probeLength = probeDirection.Length();

                if (probeLength < 0.001f) {
                    // Probe bones are too close together, can't determine direction
                    SKSE::log::debug("Probe bones too close together (probeHandle={:#x})", entry.probeHandle);
                    continue;
                }

                probeDirection = probeDirection / probeLength;  // Normalize

                // Calculate penetration depth for tip bone only
                // Vector from target to tip (using CURRENT tip position)
                RE::NiPoint3 targetToTip = tipWorld - targetPos;

                // Project onto probe direction to get penetration depth
                // Positive = probe has gone beyond target in forward direction
                // Negative = probe hasn't reached target yet
                float tipPenetration = targetToTip.Dot(probeDirection);

                SKSE::log::info(
                    "Penetration check: probeHandle={:#x} tipPenetration={:.3f} shrinkThreshold={:.3f} "
                    "restoreThreshold={:.3f}",
                    entry.probeHandle, tipPenetration, entry.distanceThreshold, entry.restoreThreshold);

                if (tipPenetration > entry.distanceThreshold) {
                    // Track max for telemetry, but drive offset from cached maximum beyond threshold
                    if (tipPenetration > entry.maxPenetration) {
                        entry.maxPenetration = tipPenetration;
                        SKSE::log::debug("New max penetration: {:.3f} (probeHandle={:#x})", entry.maxPenetration,
                                         entry.probeHandle);
                    }

                    const float currentBeyondThreshold = tipPenetration - entry.distanceThreshold;
                    bool newMaxBeyond = false;
                    if (currentBeyondThreshold > entry.maxPenetrationBeyondThreshold) {
                        entry.maxPenetrationBeyondThreshold = currentBeyondThreshold;
                        newMaxBeyond = true;
                        SKSE::log::debug("New max penetration beyond threshold: {:.3f} (probeHandle={:#x})",
                                         entry.maxPenetrationBeyondThreshold, entry.probeHandle);
                    }

                    // Use cached maximum penetration beyond threshold for offset calculation
                    float penetrationBeyondThreshold = entry.maxPenetrationBeyondThreshold;

                    // Distribute offset evenly across all middle bones
                    float distributedOffset = penetrationBeyondThreshold / static_cast<float>(middleBones.size());

                    // Clamp offset to prevent runaway feedback loop
                    distributedOffset = std::min(distributedOffset, kMaxBoneOffset);

                    // Only update bones when we achieved a new max OR they have been restored to original length
                    for (std::size_t i = 0; i < middleBones.size(); ++i) {
                        const std::size_t boneIdx = middleBoneIndices[i];
                        const bool wasMoved = entry.movedFlags[boneIdx];

                        if (!newMaxBeyond && wasMoved) {
                            continue;  // already at max
                        }

                        MoveBoneToTarget(probeActor.get(), entry.probeNodes[boneIdx], distributedOffset);
                        entry.movedFlags[boneIdx] = true;

                        if (!wasMoved) {
                            SKSE::log::info(
                                "Moved bone (probeHandle={:#x} node={} distributedOffset={:.2f} tipPenetration={:.2f} "
                                "maxPenetration={:.2f} threshold={:.2f})",
                                entry.probeHandle, GetNodeLabel(entry.probeNodes[boneIdx]), distributedOffset,
                                tipPenetration, entry.maxPenetration, entry.distanceThreshold);
                        }
                    }
                } else if (tipPenetration <= entry.restoreThreshold) {
                    // Tip is at or below restore threshold - restore all moved middle bones to original positions
                    for (std::size_t i = 0; i < middleBones.size(); ++i) {
                        const std::size_t boneIdx = middleBoneIndices[i];
                        if (entry.movedFlags[boneIdx]) {
                            RestoreBonePosition(probeActor.get(), entry.probeNodes[boneIdx]);
                            entry.movedFlags[boneIdx] = false;
                            SKSE::log::info(
                                "Restored bone (probeHandle={:#x} node={} tipPenetration={:.2f} "
                                "restoreThreshold={:.2f})",
                                entry.probeHandle, GetNodeLabel(entry.probeNodes[boneIdx]), tipPenetration,
                                entry.restoreThreshold);
                        }
                    }
                    // Keep maxPenetration - it represents the learned maximum for this looped animation
                    // Only reset when monitor is removed/recreated
                }
                // else: tipPenetration is between restoreThreshold and distanceThreshold - maintain current state
            }

            // Remove monitors in reverse order to maintain indices
            for (auto it = monitorsToRemove.rbegin(); it != monitorsToRemove.rend(); ++it) {
                if (*it < s_monitors.size()) {
                    s_monitors.erase(s_monitors.begin() + *it);
                }
            }

            // Check if we still have active monitors
            if (s_monitors.empty()) {
                // Use atomic instead of mutex to avoid potential deadlock
                s_uiTickActive.store(false, std::memory_order_release);
                SKSE::log::info("No more active monitors, stopping tick.");
                return;
            }

            // Schedule next tick
            ScheduleNextTick();
        }
    }  // namespace Monitoring
}  // namespace

namespace Papyrus {
    bool RegisterBoneMonitor(RE::StaticFunctionTag*, RE::Actor* probeActor,
                             RE::reference_array<RE::BSFixedString> probeNodeNames, RE::Actor* targetActor,
                             RE::BSFixedString targetNodeName, float distanceThreshold, float restoreThreshold) {
        SKSE::log::info(
            "RegisterBoneMonitor invoked (probeActor={}, targetActor={}, shrinkThreshold={:.2f}, "
            "restoreThreshold={:.2f}, probeNodes={})",
            static_cast<const void*>(probeActor), static_cast<const void*>(targetActor), distanceThreshold,
            restoreThreshold, probeNodeNames.size());

        if (!probeActor || !targetActor) {
            PrintToConsole("RegisterBoneMonitor: invalid actor arguments.");
            return false;
        }

        if (probeNodeNames.empty()) {
            PrintToConsole("RegisterBoneMonitor: probe node list must be non-empty.");
            return false;
        }

        std::vector<RE::BSFixedString> probeNodes;
        probeNodes.reserve(probeNodeNames.size());
        for (const auto& name : probeNodeNames) {
            const char* data = name.data();
            if (!data || *data == '\0') {
                PrintToConsole("RegisterBoneMonitor: probe node names must be non-empty.");
                return false;
            }
            probeNodes.push_back(name);
        }

        // Require at least base, middle, and tip bones
        if (probeNodes.size() < 3) {
            PrintToConsole("RegisterBoneMonitor: probe node list must contain at least 3 nodes (base, middle, tip).");
            return false;
        }

        const char* targetNodeData = targetNodeName.data();
        if (!targetNodeData || *targetNodeData == '\0') {
            PrintToConsole("RegisterBoneMonitor: target node name must be non-empty.");
            return false;
        }

        if (!Monitoring::AddMonitor(probeActor, probeNodes, targetActor, targetNodeName, distanceThreshold,
                                    restoreThreshold)) {
            PrintToConsole("RegisterBoneMonitor: failed to start monitoring.");
            return false;
        }

        PrintToConsole(fmt::format(
            "RegisterBoneMonitor: monitoring {}.[{}] -> {}.{} (shrink {:.2f}, restore {:.2f}, lifetime until stopped)",
            GetActorName(probeActor), JoinNodeLabels(probeNodes), GetActorName(targetActor),
            GetNodeLabel(targetNodeName), distanceThreshold, restoreThreshold));
        return true;
    }

    bool StopBoneMonitor(RE::StaticFunctionTag*, RE::reference_array<RE::Actor*> actors) {
        std::vector<std::uint32_t> handles;
        handles.reserve(actors.size());

        for (const auto& actor : actors) {
            if (actor) {
                const auto handle = actor->GetHandle().native_handle();
                if (handle != 0) {
                    handles.push_back(handle);
                }
            }
        }

        const auto removed = Monitoring::RemoveMonitors(handles);
        SKSE::log::info("StopBoneMonitor invoked (requested actors={}, removed monitors={})", handles.size(), removed);
        if (removed == 0) {
            if (handles.empty()) {
                PrintToConsole("StopBoneMonitor: no active monitors.");
            } else {
                PrintToConsole(fmt::format("StopBoneMonitor: no monitors matched {} actor(s).", handles.size()));
            }
            return false;
        }

        if (handles.empty()) {
            PrintToConsole(fmt::format("StopBoneMonitor: stopped all {} monitor(s).", removed));
        } else {
            PrintToConsole(
                fmt::format("StopBoneMonitor: stopped {} monitor(s) for {} actor(s).", removed, handles.size()));
        }

        SKSE::log::info("StopBoneMonitor removed {} monitor(s)", removed);
        return true;
    }

    void SetTickInterval(RE::StaticFunctionTag*, int intervalMs) {
        SKSE::log::info("SetTickInterval invoked (intervalMs={})", intervalMs);
        Monitoring::SetTickInterval(intervalMs);
        PrintToConsole(fmt::format("SetTickInterval: interval set to {}ms", Monitoring::GetTickInterval()));
    }

    int GetTickInterval(RE::StaticFunctionTag*) {
        const int interval = Monitoring::GetTickInterval();
        SKSE::log::info("GetTickInterval invoked, returning {}ms", interval);
        return interval;
    }

    bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm) {
        vm->RegisterFunction("RegisterBoneMonitor"sv, "KnowYourLimits"sv, RegisterBoneMonitor);
        vm->RegisterFunction("StopBoneMonitor"sv, "KnowYourLimits"sv, StopBoneMonitor);
        vm->RegisterFunction("SetTickInterval"sv, "KnowYourLimits"sv, SetTickInterval);
        vm->RegisterFunction("GetTickInterval"sv, "KnowYourLimits"sv, GetTickInterval);
        SKSE::log::info("Papyrus functions registered.");
        return true;
    }
}  // namespace Papyrus

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);

    SetupLogging();
    SKSE::log::info("Know Your Limits plugin loading...");

    // Note: monitoring code obtains the task interface lazily with SKSE::GetTaskInterface().

    if (const auto* messaging = SKSE::GetMessagingInterface()) {
        if (!messaging->RegisterListener([](SKSE::MessagingInterface::Message* message) {
                switch (message->type) {
                    case SKSE::MessagingInterface::kPostLoadGame:
                    case SKSE::MessagingInterface::kNewGame:
                        SKSE::log::info("New game/Load: cleaning up monitoring system and restoring bones.");
                        Monitoring::Shutdown();
                        break;

                    case SKSE::MessagingInterface::kDataLoaded:
                        SKSE::log::info("Data loaded successfully.");
                        if (auto* console = RE::ConsoleLog::GetSingleton()) {
                            console->Print("Know Your Limits: Ready");
                        }
                        break;

                    default:
                        break;
                }
            })) {
            SKSE::log::critical("Failed to register messaging listener.");
            return false;
        }
    } else {
        SKSE::log::critical("Messaging interface unavailable.");
        return false;
    }

    if (const auto* papyrus = SKSE::GetPapyrusInterface()) {
        if (!papyrus->Register(Papyrus::RegisterFunctions)) {
            SKSE::log::critical("Failed to register Papyrus functions.");
            return false;
        }
    } else {
        SKSE::log::critical("Papyrus interface unavailable.");
        return false;
    }

    SKSE::log::info("Know Your Limits plugin loaded successfully.");
    return true;
}

// Note: We intentionally do NOT use a global destructor for cleanup.
// Static destruction order is undefined, and spdlog/SKSE statics may already
// be destroyed when our destructor runs, causing crashes.
// Instead, we rely on game state messages (kPostLoadGame, kNewGame) to cleanup,
// which is the proper way to handle this in SKSE plugins.
