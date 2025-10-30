#include <fmt/format.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "PCH.h"
#include "RE/N/NiAVObject.h"
#include "RE/R/ReferenceArray.h"

namespace {
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
            return std::string{"<none>"};
        }

        if (const char* name = actor->GetDisplayFullName(); name && *name != '\0') {
            return name;
        }

        if (const char* baseName = actor->GetName(); baseName && *baseName != '\0') {
            return baseName;
        }

        return std::string{"<unnamed>"};
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
            std::chrono::steady_clock::time_point expirationTime{};
            float lifetimeSeconds{0.0f};
            float distanceThreshold{0.0f};
            std::vector<bool> scaledFlags;
            bool waitingForBones{false};
        };

        std::mutex s_monitorMutex;
        std::vector<MonitorEntry> s_monitors;

        std::mutex s_scaledMutex;
        std::unordered_map<std::uint32_t, std::unordered_map<std::string, float>> s_scaledBones;

        std::mutex s_uiTickMutex;
        bool s_uiTickActive = false;
        std::chrono::steady_clock::time_point s_lastTickTime{};

        // Run the monitor tick at most 4 times per second to avoid UI thread churn.
        constexpr std::chrono::milliseconds kTickInterval{250};
        constexpr float kScaledDownScale = 0.1f;
        constexpr float kScaleTolerance = 0.001f;  // More reasonable than epsilon for scale comparisons

        void ProcessTick();

        void QueueTick() {
            std::lock_guard<std::mutex> lk(s_uiTickMutex);
            if (s_uiTickActive) {
                return;
            }

            if (auto* task = SKSE::GetTaskInterface()) {
                s_uiTickActive = true;
                s_lastTickTime = std::chrono::steady_clock::now();
                task->AddUITask([]() { ProcessTick(); });
            } else {
                SKSE::log::critical("Task interface unavailable; cannot start monitor updates.");
            }
        }

        void ScheduleNextTick() {
            // Use a detached thread ONLY for the sleep, then queue the actual work on UI thread
            // This avoids freezing the UI thread while still using SKSE's task system
            std::thread([task = SKSE::GetTaskInterface()]() {
                if (!task) {
                    std::lock_guard<std::mutex> lk(s_uiTickMutex);
                    s_uiTickActive = false;
                    SKSE::log::critical("Task interface unavailable; stopping monitor updates.");
                    return;
                }

                const auto now = std::chrono::steady_clock::now();
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastTickTime);

                // Sleep on background thread to avoid blocking UI
                if (elapsed < kTickInterval) {
                    std::this_thread::sleep_for(kTickInterval - elapsed);
                } else {
                    // If we're behind schedule, still sleep a minimum amount
                    std::this_thread::sleep_for(kTickInterval);
                }

                // Queue the actual processing on UI thread
                task->AddUITask([]() {
                    s_lastTickTime = std::chrono::steady_clock::now();
                    ProcessTick();
                });
            }).detach();
        }

        void StopAllMonitoring() {
            std::lock_guard<std::mutex> lk(s_uiTickMutex);
            s_uiTickActive = false;
            SKSE::log::info("Monitoring system stopped.");
        }

        void ScaleBoneToTarget(std::uint32_t actorHandle, RE::NiAVObject* node, const RE::BSFixedString& nodeName) {
            if (!node) {
                return;
            }

            const std::string nodeKey{GetNodeLabel(nodeName)};

            // Read the scale first before modifying anything
            const float originalScale = node->local.scale;

            // Check if scale value is reasonable (not NaN, not inf, not negative)
            if (!std::isfinite(originalScale) || originalScale < 0.0f) {
                SKSE::log::warn("Node {} has invalid scale value: {}", nodeKey, originalScale);
                return;
            }

            {
                std::lock_guard<std::mutex> lock(s_scaledMutex);
                auto& actorBones = s_scaledBones[actorHandle];
                actorBones.try_emplace(nodeKey, originalScale);
            }

            if (std::fabs(node->local.scale - kScaledDownScale) > kScaleTolerance) {
                node->local.scale = kScaledDownScale;
                // DO NOT call UpdateWorldData - it's not thread-safe and will crash
                // The game will update world transforms on its next animation frame
            }
        }

        std::pair<std::size_t, std::size_t> RestoreScaledBones(const std::vector<std::uint32_t>& handles) {
            std::vector<std::uint32_t> targetHandles = handles;
            std::vector<std::pair<std::uint32_t, std::vector<std::pair<std::string, float>>>> pending;
            {
                std::lock_guard<std::mutex> lock(s_scaledMutex);
                if (targetHandles.empty()) {
                    targetHandles.reserve(s_scaledBones.size());
                    for (const auto& entry : s_scaledBones) {
                        targetHandles.push_back(entry.first);
                    }
                }

                pending.reserve(targetHandles.size());
                for (const auto handle : targetHandles) {
                    auto it = s_scaledBones.find(handle);
                    if (it == s_scaledBones.end()) {
                        continue;
                    }

                    std::vector<std::pair<std::string, float>> bones;
                    bones.reserve(it->second.size());
                    for (const auto& [nodeName, originalScale] : it->second) {
                        bones.emplace_back(nodeName, originalScale);
                    }

                    pending.emplace_back(handle, std::move(bones));
                    s_scaledBones.erase(it);
                }
            }

            std::size_t restored = 0;
            std::size_t processedActors = 0;
            std::vector<std::pair<std::uint32_t, std::vector<std::pair<std::string, float>>>> deferred;
            deferred.reserve(pending.size());
            for (auto& actorEntry : pending) {
                RE::NiPointer<RE::Actor> actor;
                if (!RE::Actor::LookupByHandle(static_cast<RE::RefHandle>(actorEntry.first), actor) || !actor) {
                    continue;
                }

                if (!actor->Is3DLoaded()) {
                    // Defer restoration until the actor's 3D is ready; requeue below.
                    SKSE::log::debug(
                        "ResetScaledBones deferring restore for {} (handle={:#010x}) because 3D is not loaded.",
                        GetActorName(actor.get()), actorEntry.first);
                    deferred.emplace_back(actorEntry.first, std::move(actorEntry.second));
                    continue;
                }

                ++processedActors;
                for (auto& boneEntry : actorEntry.second) {
                    RE::BSFixedString nodeName{boneEntry.first.c_str()};
                    auto* node = actor->GetNodeByName(nodeName);
                    if (!node) {
                        continue;
                    }

                    // Check if the scale value is valid before restoring
                    if (!std::isfinite(boneEntry.second) || boneEntry.second < 0.0f) {
                        SKSE::log::warn("Skipping restore of {} with invalid scale: {}", boneEntry.first,
                                        boneEntry.second);
                        continue;
                    }

                    if (std::fabs(node->local.scale - boneEntry.second) > kScaleTolerance) {
                        node->local.scale = boneEntry.second;
                        // DO NOT call UpdateWorldData - it's not thread-safe and causes crashes
                        // The game will automatically update world transforms during its animation update
                    }

                    ++restored;
                }
            }

            if (!deferred.empty()) {
                std::lock_guard<std::mutex> lock(s_scaledMutex);
                for (auto& deferredEntry : deferred) {
                    auto& actorBones = s_scaledBones[deferredEntry.first];
                    for (auto& boneEntry : deferredEntry.second) {
                        actorBones[boneEntry.first] = boneEntry.second;
                    }
                }
            }

            return {restored, processedActors};
        }

        bool QueueRestoreScaledBones(std::vector<std::uint32_t> handles, std::size_t requestedCount) {
            bool hasTargets = false;
            {
                std::lock_guard<std::mutex> lock(s_scaledMutex);
                if (handles.empty()) {
                    hasTargets = !s_scaledBones.empty();
                } else {
                    for (const auto handle : handles) {
                        if (s_scaledBones.find(handle) != s_scaledBones.end()) {
                            hasTargets = true;
                            break;
                        }
                    }
                }
            }

            if (!hasTargets) {
                SKSE::log::info("Async scaled bone restore skipped (requested actors={}, no tracked bones).",
                                requestedCount);
                PrintToConsole("ResetScaledBones: no scaled bones to restore.");
                return false;
            }

            auto* task = SKSE::GetTaskInterface();
            if (!task) {
                SKSE::log::critical("Task interface unavailable; cannot queue scaled bone restore.");
                PrintToConsole("ResetScaledBones: task interface unavailable.");
                return false;
            }

            task->AddUITask([handles = std::move(handles), requestedCount]() mutable {
                const auto [restoredBones, actorCount] = RestoreScaledBones(handles);
                SKSE::log::info(
                    "Async scaled bone restore complete (requested actors={}, restored actors={}, restored bones={})",
                    requestedCount, actorCount, restoredBones);

                if (restoredBones == 0) {
                    PrintToConsole("ResetScaledBones: no scaled bones to restore.");
                } else {
                    PrintToConsole(fmt::format("ResetScaledBones: restored {} bone(s) for {} actor(s).", restoredBones,
                                               actorCount));
                }
            });

            return true;
        }

        void RestoreAllScaledBonesBeforeLoad() {
            const auto [restoredBones, actorCount] = RestoreScaledBones({});
            if (restoredBones > 0) {
                SKSE::log::info("Pre-load restoration restored {} bone(s) for {} actor(s).", restoredBones, actorCount);
            } else {
                SKSE::log::debug("Pre-load restoration found no scaled bones to restore.");
            }

            std::lock_guard<std::mutex> lock(s_scaledMutex);
            if (!s_scaledBones.empty()) {
                const char* entryLabel = s_scaledBones.size() == 1 ? "entry" : "entries";
                SKSE::log::debug("Clearing {} deferred scaled bone {} before save load.", s_scaledBones.size(),
                                 entryLabel);
                s_scaledBones.clear();
            }
        }

        bool AddMonitor(RE::Actor* probeActor, const std::vector<RE::BSFixedString>& probeNodeNames,
                        RE::Actor* targetActor, const RE::BSFixedString& targetNodeName, float lifetimeSeconds,
                        float distanceThreshold) {
            if (!probeActor || !targetActor) {
                SKSE::log::warn("AddMonitor rejected null actors (probe={}, target={})",
                                static_cast<const void*>(probeActor), static_cast<const void*>(targetActor));
                return false;
            }

            if (probeNodeNames.empty()) {
                SKSE::log::warn("AddMonitor rejected empty probe node list.");
                return false;
            }

            const auto now = std::chrono::steady_clock::now();
            const float clampedLifetime = lifetimeSeconds > 0.0f ? lifetimeSeconds : 0.0f;
            const auto expiration = clampedLifetime > 0.0f
                                        ? now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                    std::chrono::duration<float>{clampedLifetime})
                                        : std::chrono::steady_clock::time_point{};
            // Don't clamp threshold - allow negative values for pre-emptive scaling
            const float threshold = distanceThreshold;
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
                        entry.expirationTime = expiration;
                        entry.lifetimeSeconds = clampedLifetime;
                        entry.distanceThreshold = threshold;
                        entry.scaledFlags.resize(entry.probeNodes.size(), false);
                        entry.waitingForBones = false;
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
                    newEntry.expirationTime = expiration;
                    newEntry.lifetimeSeconds = clampedLifetime;
                    newEntry.distanceThreshold = threshold;
                    newEntry.scaledFlags.resize(probeNodeNames.size(), false);
                    newEntry.waitingForBones = false;
                    s_monitors.push_back(std::move(newEntry));
                }
            }

            const std::string probeName = GetActorName(probeActor);
            const std::string targetName = GetActorName(targetActor);
            const auto nodeTarget = GetNodeLabel(targetNodeName);
            const std::string nodeList = JoinNodeLabels(probeNodeNames);
            const std::string lifetimeText =
                clampedLifetime > 0.0f ? fmt::format("{:.2f}s", clampedLifetime) : std::string{"indefinite"};
            const std::string thresholdText = fmt::format("{:.2f}", threshold);
            SKSE::log::info("{} bone monitor for {}.[{}] -> {}.{} (threshold {}, lifetime {})",
                            updated ? "Updated" : "Created", probeName, nodeList, targetName, nodeTarget, thresholdText,
                            lifetimeText);

            QueueTick();
            return true;
        }

        std::size_t RemoveMonitors(const std::vector<std::uint32_t>& handles) {
            std::size_t removed = 0;
            bool emptyAfter = false;

            {
                std::lock_guard<std::mutex> lock(s_monitorMutex);
                if (handles.empty()) {
                    removed = s_monitors.size();
                    s_monitors.clear();
                } else {
                    const auto before = s_monitors.size();
                    s_monitors.erase(std::remove_if(s_monitors.begin(), s_monitors.end(),
                                                    [&](const MonitorEntry& entry) {
                                                        return std::find(handles.begin(), handles.end(),
                                                                         entry.probeHandle) != handles.end() ||
                                                               std::find(handles.begin(), handles.end(),
                                                                         entry.targetHandle) != handles.end();
                                                    }),
                                     s_monitors.end());
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

            // Clear all monitors
            {
                std::lock_guard<std::mutex> lock(s_monitorMutex);
                const auto count = s_monitors.size();
                s_monitors.clear();
                if (count > 0) {
                    SKSE::log::info("Cleared {} monitor(s)", count);
                }
            }

            // Restore all scaled bones
            RestoreAllScaledBonesBeforeLoad();

            SKSE::log::info("Monitoring system shutdown complete.");
        }

        void ProcessTick() {
            const auto now = std::chrono::steady_clock::now();

            // Copy monitors to process without holding the lock for the entire operation
            std::vector<MonitorEntry> monitorsToProcess;
            {
                std::lock_guard<std::mutex> lock(s_monitorMutex);
                monitorsToProcess = s_monitors;
            }

            if (monitorsToProcess.empty()) {
                std::lock_guard<std::mutex> lk(s_uiTickMutex);
                s_uiTickActive = false;
                return;
            }

            // Track which monitors to remove and which to keep
            std::vector<std::size_t> monitorsToRemove;

            for (std::size_t monitorIdx = 0; monitorIdx < monitorsToProcess.size(); ++monitorIdx) {
                auto& entry = monitorsToProcess[monitorIdx];

                if (entry.probeNodes.empty()) {
                    SKSE::log::warn("Removing monitor with no probe nodes (probeHandle={:#x} targetHandle={:#x})",
                                    entry.probeHandle, entry.targetHandle);
                    monitorsToRemove.push_back(monitorIdx);
                    continue;
                }

                RE::NiPointer<RE::Actor> probeActor;
                const bool probeValid =
                    RE::Actor::LookupByHandle(static_cast<RE::RefHandle>(entry.probeHandle), probeActor) && probeActor;

                RE::NiPointer<RE::Actor> targetActor;
                const bool targetValid =
                    RE::Actor::LookupByHandle(static_cast<RE::RefHandle>(entry.targetHandle), targetActor) &&
                    targetActor;

                if (!probeValid || !targetValid) {
                    SKSE::log::info("Removing monitor (missing actor) probeHandle={:#x} targetHandle={:#x}",
                                    entry.probeHandle, entry.targetHandle);
                    monitorsToRemove.push_back(monitorIdx);
                    continue;
                }

                // Check expiration
                if (entry.expirationTime.time_since_epoch().count() != 0 && now >= entry.expirationTime) {
                    SKSE::log::info("Removing monitor (expired) probeHandle={:#x} targetHandle={:#x} after {:.2f}s",
                                    entry.probeHandle, entry.targetHandle, entry.lifetimeSeconds);
                    monitorsToRemove.push_back(monitorIdx);
                    continue;
                }

                entry.scaledFlags.resize(entry.probeNodes.size(), false);

                auto* targetNode = targetActor->GetNodeByName(entry.targetNode);
                std::vector<RE::NiAVObject*> probeNodePtrs(entry.probeNodes.size(), nullptr);
                bool missingBones = false;

                for (std::size_t idx = 0; idx < entry.probeNodes.size(); ++idx) {
                    probeNodePtrs[idx] = probeActor->GetNodeByName(entry.probeNodes[idx]);
                    if (!probeNodePtrs[idx]) {
                        missingBones = true;
                        break;
                    }
                }

                if (!targetNode || missingBones) {
                    if (!entry.waitingForBones) {
                        entry.waitingForBones = true;
                        SKSE::log::info("Waiting for bones (probeHandle={:#x} targetHandle={:#x})", entry.probeHandle,
                                        entry.targetHandle);
                    }
                    continue;
                }

                if (entry.waitingForBones) {
                    entry.waitingForBones = false;
                    SKSE::log::info("Bones recovered (probeHandle={:#x} targetHandle={:#x})", entry.probeHandle,
                                    entry.targetHandle);
                }

                // Calculate directional penetration depths for all probe nodes
                // Use probe bone chain to determine forward direction
                std::vector<float> penetrationDepths(entry.probeNodes.size(), -std::numeric_limits<float>::max());
                bool hadInvalidNodes = false;

                // Need at least 2 probe nodes to determine direction
                if (probeNodePtrs.size() < 2) {
                    SKSE::log::warn("Need at least 2 probe nodes for directional detection (probeHandle={:#x})",
                                    entry.probeHandle);
                    continue;
                }

                // Calculate probe chain direction vector (base -> tip)
                auto* baseNode = probeNodePtrs[0];
                auto* tipNode = probeNodePtrs[probeNodePtrs.size() - 1];

                if (!baseNode || !tipNode || !targetNode) {
                    hadInvalidNodes = true;
                }

                if (!hadInvalidNodes) {
                    // Get probe direction from first to last bone
                    const auto& basePos = baseNode->world.translate;
                    const auto& tipPos = tipNode->world.translate;
                    const auto& targetPos = targetNode->world.translate;

                    // Calculate direction vector (normalized)
                    RE::NiPoint3 probeDirection = tipPos - basePos;
                    const float probeLength = probeDirection.Length();

                    if (probeLength < 0.001f) {
                        // Probe bones are too close together, can't determine direction
                        SKSE::log::debug("Probe bones too close together (probeHandle={:#x})", entry.probeHandle);
                        continue;
                    }

                    probeDirection = probeDirection / probeLength;  // Normalize

                    // For each probe node, calculate how far it has penetrated beyond the target
                    // along the probe's forward direction
                    for (std::size_t idx = 0; idx < entry.probeNodes.size(); ++idx) {
                        auto* probeNode = probeNodePtrs[idx];
                        if (!probeNode) {
                            hadInvalidNodes = true;
                            continue;
                        }

                        // Vector from target to this probe node
                        RE::NiPoint3 targetToProbe = probeNode->world.translate - targetPos;

                        // Project onto probe direction to get penetration depth
                        // Positive = probe has gone beyond target in forward direction
                        // Negative = probe hasn't reached target yet
                        penetrationDepths[idx] = targetToProbe.Dot(probeDirection);

                        // Debug logging: show penetration for each node
                        SKSE::log::debug(
                            "Penetration check: probeHandle={:#x} node[{}]={} penetration={:.3f} threshold={:.3f}",
                            entry.probeHandle, idx, GetNodeLabel(entry.probeNodes[idx]), penetrationDepths[idx],
                            entry.distanceThreshold);
                    }
                }

                // If nodes became invalid during processing, skip scaling this frame
                if (hadInvalidNodes) {
                    SKSE::log::debug("Skipping frame for probeHandle={:#x} (invalid nodes)", entry.probeHandle);
                    continue;
                }

                // Find the first node that has penetrated beyond threshold
                // Scale it and all subsequent nodes (cascade effect)
                int firstPenetratedIndex = -1;
                float maxPenetration = -std::numeric_limits<float>::max();

                // Check all nodes for threshold breach (works for positive, zero, or negative thresholds)
                for (std::size_t idx = 0; idx < penetrationDepths.size(); ++idx) {
                    // Check if this node has penetrated beyond the threshold
                    if (penetrationDepths[idx] > entry.distanceThreshold) {
                        if (firstPenetratedIndex == -1) {
                            firstPenetratedIndex = static_cast<int>(idx);
                        }
                        if (penetrationDepths[idx] > maxPenetration) {
                            maxPenetration = penetrationDepths[idx];
                        }
                    }
                }

                // Scale bones if penetration threshold breached
                if (firstPenetratedIndex != -1) {
                    // Scale from the first penetrated node onwards (cascade to tail)
                    for (std::size_t idx = static_cast<std::size_t>(firstPenetratedIndex); idx < probeNodePtrs.size();
                         ++idx) {
                        auto* node = probeNodePtrs[idx];
                        if (!node) {
                            continue;
                        }

                        const bool wasScaled = entry.scaledFlags[idx];
                        ScaleBoneToTarget(entry.probeHandle, node, entry.probeNodes[idx]);
                        entry.scaledFlags[idx] = true;

                        if (!wasScaled) {
                            SKSE::log::info(
                                "Scaled bone (probeHandle={:#x} node={} penetration={:.2f} threshold={:.2f})",
                                entry.probeHandle, GetNodeLabel(entry.probeNodes[idx]), penetrationDepths[idx],
                                entry.distanceThreshold);
                        }
                    }
                }
            }

            // Update original monitors and remove expired/invalid ones
            {
                std::lock_guard<std::mutex> lock(s_monitorMutex);

                // Remove monitors in reverse order to maintain indices
                for (auto it = monitorsToRemove.rbegin(); it != monitorsToRemove.rend(); ++it) {
                    if (*it < s_monitors.size()) {
                        s_monitors.erase(s_monitors.begin() + *it);
                    }
                }

                // Update scaled flags for remaining monitors
                for (std::size_t i = 0; i < s_monitors.size() && i < monitorsToProcess.size(); ++i) {
                    if (s_monitors[i].probeHandle == monitorsToProcess[i].probeHandle &&
                        s_monitors[i].targetHandle == monitorsToProcess[i].targetHandle) {
                        s_monitors[i].scaledFlags = monitorsToProcess[i].scaledFlags;
                        s_monitors[i].waitingForBones = monitorsToProcess[i].waitingForBones;
                    }
                }

                // Check if we still have active monitors
                if (s_monitors.empty()) {
                    std::lock_guard<std::mutex> lk(s_uiTickMutex);
                    s_uiTickActive = false;
                    SKSE::log::info("No more active monitors, stopping tick.");
                    return;
                }
            }

            // Schedule next tick
            ScheduleNextTick();
        }
    }  // namespace Monitoring
}  // namespace

namespace Papyrus {
    bool RegisterBoneMonitor(RE::StaticFunctionTag*, RE::Actor* probeActor,
                             RE::reference_array<RE::BSFixedString> probeNodeNames, RE::Actor* targetActor,
                             RE::BSFixedString targetNodeName, float duration, float distanceThreshold) {
        SKSE::log::info(
            "RegisterBoneMonitor invoked (probeActor={}, targetActor={}, duration={:.2f}, threshold={:.2f}, "
            "probeNodes={})",
            static_cast<const void*>(probeActor), static_cast<const void*>(targetActor), duration, distanceThreshold,
            probeNodeNames.size());

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

        const char* targetNodeData = targetNodeName.data();
        if (!targetNodeData || *targetNodeData == '\0') {
            PrintToConsole("RegisterBoneMonitor: target node name must be non-empty.");
            return false;
        }

        const float lifetimeSeconds = duration > 0.0f ? duration : 0.0f;
        if (!Monitoring::AddMonitor(probeActor, probeNodes, targetActor, targetNodeName, lifetimeSeconds,
                                    distanceThreshold)) {
            PrintToConsole("RegisterBoneMonitor: failed to start monitoring.");
            return false;
        }

        const std::string lifetimeText =
            lifetimeSeconds > 0.0f ? fmt::format("{:.2f}s", lifetimeSeconds) : std::string{"until stopped"};
        const std::string thresholdText = fmt::format("{:.2f}", distanceThreshold);
        const std::string probeNodeList = JoinNodeLabels(probeNodes);
        PrintToConsole(fmt::format("RegisterBoneMonitor: monitoring {}.[{}] -> {}.{} (threshold {}, lifetime {})",
                                   GetActorName(probeActor), probeNodeList, GetActorName(targetActor),
                                   GetNodeLabel(targetNodeName), thresholdText, lifetimeText));
        return true;
    }

    bool StopBoneMonitor(RE::StaticFunctionTag*, RE::reference_array<RE::Actor*> actors) {
        std::vector<std::uint32_t> handles;
        handles.reserve(actors.size());

        for (std::uint32_t i = 0; i < actors.size(); ++i) {
            if (auto* actor = actors[i]) {
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

    bool ResetScaledBones(RE::StaticFunctionTag*, RE::reference_array<RE::Actor*> actors) {
        std::vector<std::uint32_t> handles;
        handles.reserve(actors.size());

        for (std::uint32_t i = 0; i < actors.size(); ++i) {
            if (auto* actor = actors[i]) {
                const auto handle = actor->GetHandle().native_handle();
                if (handle != 0) {
                    handles.push_back(handle);
                }
            }
        }

        const std::size_t requestedActors = handles.size();
        if (!Monitoring::QueueRestoreScaledBones(std::move(handles), requestedActors)) {
            SKSE::log::warn("ResetScaledBones invoked but restore task was not queued (requested actors={}).",
                            requestedActors);
            return false;
        }

        SKSE::log::info("ResetScaledBones invoked (requested actors={}, task queued)", requestedActors);
        return true;
    }

    bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm) {
        vm->RegisterFunction("RegisterBoneMonitor"sv, "KnowYourLimits"sv, RegisterBoneMonitor);
        vm->RegisterFunction("StopBoneMonitor"sv, "KnowYourLimits"sv, StopBoneMonitor);
        vm->RegisterFunction("ResetScaledBones"sv, "KnowYourLimits"sv, ResetScaledBones);
        SKSE::log::info("Papyrus functions registered.");
        return true;
    }
}  // namespace Papyrus

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);

    SetupLogging();
    SKSE::log::info("Know Your Limits plugin loading...");

    if (const auto* messaging = SKSE::GetMessagingInterface()) {
        if (!messaging->RegisterListener([](SKSE::MessagingInterface::Message* message) {
                switch (message->type) {
                    case SKSE::MessagingInterface::kPreLoadGame:
                        SKSE::log::info("PreLoadGame: restoring scaled bones before loading save.");
                        Monitoring::RestoreAllScaledBonesBeforeLoad();
                        break;

                    case SKSE::MessagingInterface::kPostLoadGame:
                    case SKSE::MessagingInterface::kNewGame:
                        SKSE::log::info("New game/Load: cleaning up monitoring system.");
                        Monitoring::StopAllMonitoring();
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

// Plugin unload (if SKSE supports it in the future)
// For now, we rely on game state messages to cleanup
namespace {
    struct PluginCleanup {
        ~PluginCleanup() {
            SKSE::log::info("Plugin cleanup triggered.");
            Monitoring::Shutdown();
        }
    };

    // This will be destroyed when the DLL unloads
    PluginCleanup g_cleanup;
}
